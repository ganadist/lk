include project/target/dartuinoP0.mk

# Keep the FS for loading recovery images.
include project/virtual/fs.mk

MODULES += \
  app/moot \
  app/shell \
  lib/version \
  lib/buildsig \
  target/dartuinoP0/bootloader
