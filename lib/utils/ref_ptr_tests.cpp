// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <stdio.h>
#include <unittest.h>
#include <utils/ref_ptr.h>

namespace {

class RefCallCounter {
public:
    RefCallCounter();

    void AddRef();
    bool Release();

    void Adopt() {}

    int add_ref_calls() const { return add_ref_calls_; }
    int release_calls() const { return release_calls_; }

private:
    int add_ref_calls_;
    int release_calls_;
};

RefCallCounter::RefCallCounter()
    : add_ref_calls_(0u), release_calls_(0u) {}

void RefCallCounter::AddRef() {
    add_ref_calls_++;
}
bool RefCallCounter::Release() {
    release_calls_++;
    return false;
}

static bool ref_ptr_test(void* context) {
    BEGIN_TEST;
    using RefCallPtr = utils::RefPtr<RefCallCounter>;

    RefCallCounter counter;
    RefCallPtr ptr = utils::AdoptRef<RefCallCounter>(&counter);

    EXPECT_TRUE(&counter == ptr.get(), ".get() should point to object");
    EXPECT_TRUE(static_cast<bool>(ptr), "operator bool");
    EXPECT_TRUE(&counter == &(*ptr), "operator*");

    // Adoption should not manipulate the refcount.
    EXPECT_EQ(0, counter.add_ref_calls(), "");
    EXPECT_EQ(0, counter.release_calls(), "");

    {
        RefCallPtr ptr2 = ptr;

        // Copying to a new RefPtr should call add once.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(0, counter.release_calls(), "");
    }
    // Destroying the new RefPtr should release once.
    EXPECT_EQ(1, counter.add_ref_calls(), "");
    EXPECT_EQ(1, counter.release_calls(), "");

    {
        RefCallPtr ptr2;

        EXPECT_TRUE(!static_cast<bool>(ptr2), "");

        ptr.swap(ptr2);

        // Swapping shouldn't cause any add or release calls, but should update
        // values.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(1, counter.release_calls(), "");

        EXPECT_TRUE(!static_cast<bool>(ptr), "");
        EXPECT_TRUE(&counter == ptr2.get(), "");

        ptr2.swap(ptr);
    }

    EXPECT_EQ(1, counter.add_ref_calls(), "");
    EXPECT_EQ(1, counter.release_calls(), "");

    {
        RefCallPtr ptr2 = utils::move(ptr);

        // Moving shouldn't cause any add or release but should update values.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(1, counter.release_calls(), "");

        EXPECT_FALSE(static_cast<bool>(ptr), "");
        EXPECT_TRUE(&counter == ptr2.get(), "");

        ptr2.swap(ptr);
    }

    // Reset should calls release and clear out the pointer.
    ptr.reset(nullptr);
    EXPECT_EQ(2, counter.release_calls(), "");
    EXPECT_FALSE(static_cast<bool>(ptr), "");
    EXPECT_FALSE(ptr.get(), "");

    END_TEST;
}

static bool ref_ptr_compare_test(void* context) {
    BEGIN_TEST;
    using RefCallPtr = utils::RefPtr<RefCallCounter>;

    RefCallCounter obj1, obj2;
    RefCallPtr ptr1 = utils::AdoptRef<RefCallCounter>(&obj1);
    RefCallPtr ptr2 = utils::AdoptRef<RefCallCounter>(&obj2);
    RefCallPtr also_ptr1 = ptr1;
    RefCallPtr null_ref_ptr;

    EXPECT_TRUE (ptr1 == ptr1, "");
    EXPECT_FALSE(ptr1 != ptr1, "");

    EXPECT_FALSE(ptr1 == ptr2, "");
    EXPECT_TRUE (ptr1 != ptr2, "");

    EXPECT_TRUE (ptr1 == also_ptr1, "");
    EXPECT_FALSE(ptr1 != also_ptr1, "");

    EXPECT_TRUE (ptr1 != null_ref_ptr, "");
    EXPECT_TRUE (ptr1 != nullptr, "");
    EXPECT_TRUE (nullptr != ptr1, "");
    EXPECT_FALSE(ptr1 == null_ref_ptr, "");
    EXPECT_FALSE(ptr1 == nullptr, "");
    EXPECT_FALSE(nullptr == ptr1, "");

    EXPECT_TRUE (null_ref_ptr == nullptr, "");
    EXPECT_FALSE(null_ref_ptr != nullptr, "");
    EXPECT_TRUE (nullptr == null_ref_ptr, "");
    EXPECT_FALSE(nullptr != null_ref_ptr, "");

    END_TEST;
}

} //namespace

UNITTEST_START_TESTCASE(ref_ptr_tests)
UNITTEST("Ref Pointer", ref_ptr_test)
UNITTEST("Ref Pointer Comparison", ref_ptr_compare_test)
UNITTEST_END_TESTCASE(ref_ptr_tests, "refptrtests", "Ref Pointer Tests", NULL, NULL);
