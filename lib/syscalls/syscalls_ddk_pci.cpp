// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <dev/interrupt.h>
#include <dev/udisplay.h>
#include <kernel/vm.h>
#include <lib/user_copy.h>
#include <lib/user_copy/user_ptr.h>

#include <magenta/interrupt_dispatcher.h>
#include <magenta/interrupt_event_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/pci.h>
#include <magenta/user_copy.h>
#include <mxtl/limits.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

#if WITH_LIB_GFXCONSOLE
// If we were built with the GFX console, make sure that it is un-bound when
// user mode takes control of PCI.  Note: there should probably be a cleaner way
// of doing this.  Not all system have PCI, and (eventually) not all systems
// will attempt to initialize PCI.  Someday, there should be a different way of
// handing off from early/BSOD kernel mode graphics to user mode.
#include <lib/gfxconsole.h>
static inline void shutdown_early_init_console() {
    gfxconsole_bind_display(NULL, NULL);
}
#else
static inline void shutdown_early_init_console() { }
#endif


#if WITH_DEV_PCIE
#include <dev/pcie_bus_driver.h>
#include <magenta/pci_device_dispatcher.h>
#include <magenta/pci_interrupt_dispatcher.h>

mx_status_t sys_pci_add_subtract_io_range(mx_handle_t handle, bool mmio, uint64_t base, uint64_t len, bool add) {
    // TODO: finer grained validation
    // TODO(security): Add additional access checks
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    auto pcie = PcieBusDriver::GetDriver();
    if (pcie == nullptr) {
        return ERR_BAD_STATE;
    }

    PcieAddrSpace addr_space = mmio ? PcieAddrSpace::MMIO : PcieAddrSpace::PIO;

    if (add) {
        return pcie->AddBusRegion(base, len, addr_space);
    } else {
        return pcie->SubtractBusRegion(base, len, addr_space);
    }
}

mx_status_t sys_pci_init(mx_handle_t handle, user_ptr<mx_pci_init_arg_t> init_buf, uint32_t len) {

    // TODO: finer grained validation
    // TODO(security): Add additional access checks
    mx_status_t status;
    if ((status = validate_resource_handle(handle)) < 0) {
        return status;
    }

    mxtl::unique_ptr<mx_pci_init_arg_t, mxtl::free_delete> arg;

    if (len < sizeof(*arg) || len > MX_PCI_INIT_ARG_MAX_SIZE) {
        return ERR_INVALID_ARGS;
    }

    auto pcie = PcieBusDriver::GetDriver();
    if (pcie == nullptr)
        return ERR_BAD_STATE;

    // we have to malloc instead of new since this is a variable-sized structure
    arg.reset(static_cast<mx_pci_init_arg_t*>(malloc(len)));
    if (!arg) {
        return ERR_NO_MEMORY;
    }
    {
        mx_status_t status = init_buf.reinterpret<const void>().copy_array_from_user(arg.get(), len);
        if (status != NO_ERROR) {
            return status;
        }
    }

    const uint32_t win_count = arg->ecam_window_count;
    if (len != sizeof(*arg) + sizeof(arg->ecam_windows[0]) * win_count) {
        return ERR_INVALID_ARGS;
    }

    if (arg->num_irqs > countof(arg->irqs)) {
        return ERR_INVALID_ARGS;
    }

    // Configure interrupts
    for (unsigned int i = 0; i < arg->num_irqs; ++i) {
        uint32_t irq = arg->irqs[i].global_irq;
        enum interrupt_trigger_mode tm = IRQ_TRIGGER_MODE_EDGE;
        if (arg->irqs[i].level_triggered) {
            tm = IRQ_TRIGGER_MODE_LEVEL;
        }
        enum interrupt_polarity pol = IRQ_POLARITY_ACTIVE_LOW;
        if (arg->irqs[i].active_high) {
            pol = IRQ_POLARITY_ACTIVE_HIGH;
        }

        status_t status = configure_interrupt(irq, tm, pol);
        if (status != NO_ERROR) {
            return status;
        }
    }

    // Populate the platform swizzle map.
    // TODO(johngro) : Kill this.  See the comment in PciePlatformInterface::AddLegacySwizzle;
    // Legacy swizzling should be a property of a PCIe/PCI root, not the platform.
    for (uint dev = 0; dev < countof(arg->dev_pin_to_global_irq); ++dev) {
        for (uint func = 0; func < countof(arg->dev_pin_to_global_irq[dev]); ++func) {
            constexpr uint bus = 0;
            const auto& swiz_map_entry = arg->dev_pin_to_global_irq[dev][func];
            status_t res = pcie->platform().AddLegacySwizzle(bus, dev, func, swiz_map_entry);

            if (res != NO_ERROR) {
                TRACEF("Failed to add PCIe legacy swizzle map entry for %02x:%02x.%01x (res %d)\n",
                        bus, dev, func, res);
                return res;
            }
        }
    }

    // TODO(teisenbe): For now assume there is only one ECAM
    if (win_count != 1) {
        return ERR_INVALID_ARGS;
    }
    if (arg->ecam_windows[0].bus_start != 0) {
        return ERR_INVALID_ARGS;
    }

    if (arg->ecam_windows[0].bus_start > arg->ecam_windows[0].bus_end) {
        return ERR_INVALID_ARGS;
    }

#if ARCH_X86
    // Check for a quirk that we've seen.  Some systems will report overly large
    // PCIe config regions that collide with architectural registers.
    unsigned int num_buses = arg->ecam_windows[0].bus_end -
            arg->ecam_windows[0].bus_start + 1;
    paddr_t end = arg->ecam_windows[0].base +
            num_buses * PCIE_ECAM_BYTE_PER_BUS;
    const paddr_t high_limit = 0xfec00000ULL;
    if (end > high_limit) {
        TRACEF("PCIe config space collides with arch devices, truncating\n");
        end = high_limit;
        if (end < arg->ecam_windows[0].base) {
            return ERR_INVALID_ARGS;
        }
        arg->ecam_windows[0].size = ROUNDDOWN(end - arg->ecam_windows[0].base,
                                              PCIE_ECAM_BYTE_PER_BUS);
        uint64_t new_bus_end = (arg->ecam_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) +
                arg->ecam_windows[0].bus_start - 1;
        if (new_bus_end >= PCIE_MAX_BUSSES) {
            return ERR_INVALID_ARGS;
        }
        arg->ecam_windows[0].bus_end = static_cast<uint8_t>(new_bus_end);
    }
#endif

    if (arg->ecam_windows[0].size < PCIE_ECAM_BYTE_PER_BUS) {
        return ERR_INVALID_ARGS;
    }
    if (arg->ecam_windows[0].size / PCIE_ECAM_BYTE_PER_BUS >
        PCIE_MAX_BUSSES - arg->ecam_windows[0].bus_start) {

        return ERR_INVALID_ARGS;
    }

    // TODO(johngro): Update the syscall to pass a paddr_t for base instead of a uint64_t
    ASSERT(arg->ecam_windows[0].base < mxtl::numeric_limits<paddr_t>::max());

    // TODO(johngro): Do not limit this to a single range.  Instead, fetch all
    // of the ECAM ranges from ACPI, as well as the appropriate bus start/end
    // ranges.
    status_t ret;
    const PcieBusDriver::EcamRegion ecam {
        .phys_base = static_cast<paddr_t>(arg->ecam_windows[0].base),
        .size      = arg->ecam_windows[0].size,
        .bus_start = 0x00,
        .bus_end   = static_cast<uint8_t>((arg->ecam_windows[0].size / PCIE_ECAM_BYTE_PER_BUS) - 1),
    };

    ret = pcie->AddEcamRegion(ecam);
    if (ret != NO_ERROR) {
        TRACEF("Failed to add ECAM region to PCIe bus driver!\n");
        return ret;
    }

    // TODO(johngro): Relax this assumption when the bus driver supports
    // multiple roots.
    ret = pcie->AddRoot(0u);
    if (ret != NO_ERROR) {
        TRACEF("Failed to add root complex to PCIe bus driver!\n");
        return ret;
    }

    shutdown_early_init_console();
    return NO_ERROR;
}

mx_handle_t sys_pci_get_nth_device(mx_handle_t hrsrc, uint32_t index, mx_pcie_get_nth_info_t* out_info) {
    /**
     * Returns the pci config of a device.
     * @param index Device index
     * @param out_info Device info (BDF address, vendor id, etc...)
     */
    LTRACE_ENTRY;

    // TODO: finer grained validation
    mx_status_t status;
    if ((status = validate_resource_handle(hrsrc)) < 0) {
        return status;
    }

    if (!out_info)
        return ERR_INVALID_ARGS;

    mxtl::RefPtr<Dispatcher> dispatcher;
    mx_rights_t rights;
    mx_pcie_get_nth_info_t info;
    status_t result = PciDeviceDispatcher::Create(index, &info, &dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(mxtl::move(dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    auto up = ProcessDispatcher::GetCurrent();
    mx_handle_t handle_value = up->MapHandleToValue(handle.get());

    if (copy_to_user_unsafe(reinterpret_cast<uint8_t*>(out_info),
                            &info, sizeof(*out_info)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    up->AddHandle(mxtl::move(handle));
    return handle_value;
}

mx_status_t sys_pci_claim_device(mx_handle_t handle) {
    /**
     * Claims the PCI device associated with the handle. Called when a driver
     * successfully probes the device.
     * @param handle Handle associated with a PCI device
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->ClaimDevice();
}

mx_status_t sys_pci_enable_bus_master(mx_handle_t handle, bool enable) {
    /**
     * Enables or disables bus mastering for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param enable true if bus mastering should be enabled.
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->EnableBusMaster(enable);
}

mx_status_t sys_pci_reset_device(mx_handle_t handle) {
    /**
     * Resets the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->ResetDevice();
}

mx_handle_t sys_pci_map_mmio(mx_handle_t handle, uint32_t bar_num, mx_cache_policy_t cache_policy) {
    /**
     * Performs MMIO mapping for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param bar_num BAR number
     */
    LTRACEF("handle %d\n", handle);

    // Caller only gets to control the cache policy, nothing else.
    if (cache_policy & ~ARCH_MMU_FLAG_CACHE_MASK)
        return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    mx_rights_t mmio_rights;
    mxtl::RefPtr<Dispatcher> mmio_io_mapping;
    status_t result = pci_device->MapMmio(bar_num, cache_policy, &mmio_io_mapping, &mmio_rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr mmio_handle(MakeHandle(mxtl::move(mmio_io_mapping), mmio_rights));
    if (!handle)
        return ERR_NO_MEMORY;

    mx_handle_t ret_val = up->MapHandleToValue(mmio_handle.get());
    up->AddHandle(mxtl::move(mmio_handle));
    return ret_val;
}

mx_status_t sys_pci_io_write(mx_handle_t handle, uint32_t bar_num, uint32_t offset, uint32_t len,
                             uint32_t value) {
    /**
     * Performs port I/O write for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param bar_num BAR number
     * @param offset Offset from the base
     * @param len Length of the operation in bytes
     * @param value_ptr Pointer to the value to write
     */
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_io_read(mx_handle_t handle, uint32_t bar_num, uint32_t offset, uint32_t len,
                            uint32_t* out_value_ptr) {
    /**
     * Performs port I/O read for the PCI device associated with the handle.
     * @param handle Handle associated with a PCI device
     * @param bar_num BAR number
     * @param offset Offset from the base
     * @param len Length of the operation in bytes
     * @param out_value_ptr Pointer to read the value into
     */
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_interrupt(mx_handle_t handle_value, int32_t which_irq) {
    /**
     * Returns a handle that can be waited on.
     * @param handle Handle associated with a PCI device
     * @param which_irq Identifier for an IRQ, returned in sys_pci_get_nth_device, or -1 for legacy
     * interrupts
     */
    LTRACEF("handle %d\n", handle_value);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status =
        up->GetDispatcher(handle_value, &pci_device, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    mxtl::RefPtr<Dispatcher> interrupt_dispatcher;
    mx_rights_t rights;
    status_t result = pci_device->MapInterrupt(which_irq, &interrupt_dispatcher, &rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr handle(MakeHandle(mxtl::move(interrupt_dispatcher), rights));
    if (!handle)
        return ERR_NO_MEMORY;

    mx_handle_t interrupt_handle = up->MapHandleToValue(handle.get());
    up->AddHandle(mxtl::move(handle));
    return interrupt_handle;
}

mx_handle_t sys_pci_map_config(mx_handle_t handle) {
    /**
     * Fetch an I/O Mapping object which maps the PCI device's mmaped config
     * into the caller's address space (read only)
     *
     * @param handle Handle associated with a PCI device
     */
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    mx_rights_t config_rights;
    mxtl::RefPtr<Dispatcher> config_io_mapping;
    status_t result = pci_device->MapConfig(&config_io_mapping, &config_rights);
    if (result != NO_ERROR)
        return result;

    HandleUniquePtr config_handle(MakeHandle(mxtl::move(config_io_mapping), config_rights));
    if (!config_handle)
        return ERR_NO_MEMORY;

    mx_handle_t ret_val = up->MapHandleToValue(config_handle.get());
    up->AddHandle(mxtl::move(config_handle));
    return ret_val;
}

/**
 * Gets info about the capabilities of a PCI device's IRQ modes.
 * @param handle Handle associated with a PCI device.
 * @param mode The IRQ mode whose capabilities are to be queried.
 * @param out_len Out param which will hold the maximum number of IRQs supported by the mode.
 */
mx_status_t sys_pci_query_irq_mode_caps(mx_handle_t handle,
                                        uint32_t mode,
                                        uint32_t* out_max_irqs) {
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_READ);
    if (status != NO_ERROR)
        return status;

    uint32_t max_irqs;
    status_t result = pci_device->QueryIrqModeCaps((mx_pci_irq_mode_t)mode, &max_irqs);
    if (result != NO_ERROR)
        return result;

    if (copy_to_user_unsafe(reinterpret_cast<uint8_t*>(out_max_irqs),
                            &max_irqs, sizeof(*out_max_irqs)) != NO_ERROR)
        return ERR_INVALID_ARGS;

    return result;
}

/**
 * Selects an IRQ mode for a PCI device.
 * @param handle Handle associated with a PCI device.
 * @param mode The IRQ mode to select.
 * @param requested_irq_count The number of IRQs to select request for the given mode.
 */
mx_status_t sys_pci_set_irq_mode(mx_handle_t handle,
                                 uint32_t mode,
                                 uint32_t requested_irq_count) {
    LTRACEF("handle %d\n", handle);

    auto up = ProcessDispatcher::GetCurrent();

    mxtl::RefPtr<PciDeviceDispatcher> pci_device;
    mx_status_t status = up->GetDispatcher(handle, &pci_device, MX_RIGHT_WRITE);
    if (status != NO_ERROR)
        return status;

    return pci_device->SetIrqMode((mx_pci_irq_mode_t)mode, requested_irq_count);
}
#else  // WITH_DEV_PCIE
mx_status_t sys_pci_init(mx_handle_t, user_ptr<mx_pci_init_arg_t>, uint32_t) {
    shutdown_early_init_console();
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_add_subtract_io_range(mx_handle_t handle, bool mmio, uint64_t base, uint64_t len, bool add) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_get_nth_device(mx_handle_t, uint32_t, mx_pcie_get_nth_info_t*) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_claim_device(mx_handle_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_enable_bus_master(mx_handle_t, bool) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_reset_device(mx_handle_t) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_mmio(mx_handle_t, uint32_t, mx_cache_policy_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_io_write(mx_handle_t, uint32_t, uint32_t, uint32_t, uint32_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_io_read(mx_handle_t, uint32_t, uint32_t, uint32_t, uint32_t*) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_interrupt(mx_handle_t, int32_t) {
    return ERR_NOT_SUPPORTED;
}

mx_handle_t sys_pci_map_config(mx_handle_t) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_query_irq_mode_caps(mx_handle_t, uint32_t, uint32_t*) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t sys_pci_set_irq_mode(mx_handle_t, uint32_t, uint32_t) {
    return ERR_NOT_SUPPORTED;
}
#endif  // WITH_DEV_PCIE
