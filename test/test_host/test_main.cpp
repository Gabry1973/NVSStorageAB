// Host unit tests for NVSStorageAB over an in-memory fake NVS (fake/).
// Run with: pio test -e host
#include <string.h>
#include <unity.h>

#include "NVSStorageAB.h"
#include "nvs.h"
#include "test_types.h"

typedef NVSStorageAB<V1> StorageV1;
typedef NVSStorageAB<V2> StorageV2;
typedef NVSStorageABCore::Result R;

#define ASSERT_RESULT(expected, actual) \
    TEST_ASSERT_EQUAL_STRING(StorageV2::resultToString(expected), StorageV2::resultToString(actual))

static int defaultsCalls = 0;
static int migrateCalls = 0;
static uint16_t migratedFrom = 0;

void defV1(V1& s) {
    ++defaultsCalls;
    s.id = 1;
    s.flags = 0x11;
}

void defV2(V2& s) {
    ++defaultsCalls;
    s.id = 1;
    s.flags = 0x11;
    s.timeout = 1000;
    s.brightness = 80;
}

static void migV2(uint16_t oldVersion, V2& s) {
    ++migrateCalls;
    migratedFrom = oldVersion;
    s.flags |= 0x100;
}

// Slot blob layout: 20-byte header (sequence at offset 12), then T.
static bool hasKey(const char* key) {
    return fakenvs::store.count("ns") && fakenvs::store["ns"].count(key);
}

static std::vector<uint8_t>& blob(const char* key) {
    return fakenvs::store["ns"][key];
}

static uint32_t sequenceOf(const char* key) {
    uint32_t seq;
    memcpy(&seq, blob(key).data() + 12, sizeof(seq));
    return seq;
}

static void setSequence(const char* key, uint32_t seq) { // not covered by the CRC
    memcpy(blob(key).data() + 12, &seq, sizeof(seq));
}

void setUp() {
    fakenvs::reset();
    defaultsCalls = 0;
    migrateCalls = 0;
    migratedFrom = 0;
}

void tearDown() {
    TEST_ASSERT_EQUAL_MESSAGE(0, fakenvs::openHandles, "NVS handle leaked");
}

static void test_first_boot_load_uses_defaults_without_writing() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d;
    memset(&d, 0xAA, sizeof(d));

    ASSERT_RESULT(R::DEFAULTS_LOADED, storage.load(d));
    TEST_ASSERT_EQUAL(1000, d.timeout);
    TEST_ASSERT_EQUAL(80, d.brightness);
    TEST_ASSERT_EQUAL(0, fakenvs::writes);
    TEST_ASSERT_EQUAL_MESSAGE(0, fakenvs::store.count("ns"), "namespace must not be created");
    TEST_ASSERT_EQUAL(1, defaultsCalls);
}

static void test_first_boot_load_and_upgrade_persists_defaults() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d;

    ASSERT_RESULT(R::DEFAULTS_LOADED, storage.loadAndUpgrade(d));
    TEST_ASSERT_EQUAL(1, fakenvs::writes);
    TEST_ASSERT_TRUE(hasKey("set_A"));
    TEST_ASSERT_FALSE(hasKey("set_B"));
    TEST_ASSERT_EQUAL_UINT32(1, sequenceOf("set_A"));

    ASSERT_RESULT(R::OK, storage.loadAndUpgrade(d));
    TEST_ASSERT_EQUAL(1, fakenvs::writes);
}

static void test_save_alternates_slots_and_skips_unchanged_data() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d, r;
    storage.loadAndUpgrade(d);

    ASSERT_RESULT(R::UNCHANGED, storage.save(d));
    TEST_ASSERT_EQUAL(1, fakenvs::writes);

    d.brightness = 90;
    ASSERT_RESULT(R::OK, storage.save(d));
    TEST_ASSERT_EQUAL_UINT32(2, sequenceOf("set_B"));

    d.brightness = 91;
    ASSERT_RESULT(R::OK, storage.save(d));
    TEST_ASSERT_EQUAL_UINT32(3, sequenceOf("set_A"));

    ASSERT_RESULT(R::UNCHANGED, storage.save(d));
    TEST_ASSERT_EQUAL(3, fakenvs::writes);

    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(91, r.brightness);
}

static void test_corrupted_newest_slot_falls_back_to_other_slot() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d, r;
    storage.loadAndUpgrade(d);
    d.brightness = 90;
    storage.save(d);                 // A: 80 seq 1, B: 90 seq 2

    blob("set_B")[20] ^= 0xFF;       // payload bit flip -> CRC error
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(80, r.brightness);

    d.brightness = 95;               // must overwrite the corrupted B
    ASSERT_RESULT(R::OK, storage.save(d));
    TEST_ASSERT_EQUAL_UINT32(2, sequenceOf("set_B"));
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(95, r.brightness);

    blob("set_A").resize(10);        // truncated -> invalid header
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(95, r.brightness);
}

static void test_upgrade_keeps_appended_defaults_and_migrates_once() {
    {
        StorageV1 v1("ns", "set", 1, defV1);
        V1 a;
        v1.loadAndUpgrade(a);
        a.id = 7;
        a.flags = 5;
        ASSERT_RESULT(R::OK, v1.save(a));
    }
    StorageV2 storage("ns", "set", 2, defV2, migV2);
    V2 d, r;

    ASSERT_RESULT(R::OK, storage.loadAndUpgrade(d));
    TEST_ASSERT_EQUAL(7, d.id);
    TEST_ASSERT_EQUAL_HEX32(5 | 0x100, d.flags);
    TEST_ASSERT_EQUAL(1000, d.timeout);
    TEST_ASSERT_EQUAL(80, d.brightness);
    TEST_ASSERT_EQUAL(1, migrateCalls);
    TEST_ASSERT_EQUAL(1, migratedFrom);
    TEST_ASSERT_EQUAL(3, fakenvs::writes);           // re-saved in V2 format
    TEST_ASSERT_EQUAL_UINT32(3, sequenceOf("set_A"));

    migrateCalls = 0;
    ASSERT_RESULT(R::OK, storage.loadAndUpgrade(r));
    TEST_ASSERT_EQUAL(0, migrateCalls);
    TEST_ASSERT_EQUAL(3, fakenvs::writes);
    TEST_ASSERT_EQUAL_MEMORY(&d, &r, sizeof(d));
}

static void test_load_migrates_old_version_in_ram_only() {
    {
        StorageV1 v1("ns", "set", 1, defV1);
        V1 a;
        v1.loadAndUpgrade(a);
    }
    StorageV2 storage("ns", "set", 2, defV2, migV2);
    V2 d;

    ASSERT_RESULT(R::OK, storage.load(d));
    TEST_ASSERT_EQUAL(1, migrateCalls);
    TEST_ASSERT_EQUAL(1, fakenvs::writes);
}

static void test_future_version_is_not_loaded_nor_overwritten() {
    {
        StorageV2 v3("ns", "set", 3, defV2);
        V2 a;
        v3.loadAndUpgrade(a);
        a.brightness = 1;
        v3.save(a);
    }
    StorageV2 storage("ns", "set", 2, defV2);
    V2 d;

    ASSERT_RESULT(R::FUTURE_VERSION, storage.load(d));
    TEST_ASSERT_EQUAL(80, d.brightness);
    ASSERT_RESULT(R::FUTURE_VERSION, storage.loadAndUpgrade(d));
    TEST_ASSERT_EQUAL(2, fakenvs::writes);
}

static void test_save_refuses_to_write_when_a_slot_is_unreadable() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d;
    storage.loadAndUpgrade(d);
    d.brightness = 90;
    storage.save(d);                 // B is the newest copy

    fakenvs::failReadKeys.insert("set_B");
    d.brightness = 99;
    ASSERT_RESULT(R::READ_ERROR, storage.save(d));
    TEST_ASSERT_EQUAL(2, fakenvs::writes);

    fakenvs::failReadKeys.insert("set_A");
    ASSERT_RESULT(R::READ_ERROR, storage.save(d));
    TEST_ASSERT_EQUAL(2, fakenvs::writes);
}

static void test_unreadable_slots_are_not_replaced_by_defaults() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d, r;
    storage.loadAndUpgrade(d);
    d.brightness = 90;
    storage.save(d);

    fakenvs::failReadKeys.insert("set_A");
    fakenvs::failReadKeys.insert("set_B");
    ASSERT_RESULT(R::READ_ERROR, storage.loadAndUpgrade(r));
    TEST_ASSERT_EQUAL(80, r.brightness);             // defaults in RAM
    TEST_ASSERT_EQUAL(2, fakenvs::writes);           // nothing written
    ASSERT_RESULT(R::READ_ERROR, storage.load(r));

    fakenvs::failReadKeys.clear();
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(90, r.brightness);
}

static void test_write_failure_keeps_previous_data() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d, r;
    storage.loadAndUpgrade(d);

    fakenvs::failWrite = true;
    d.brightness = 5;
    ASSERT_RESULT(R::WRITE_ERROR, storage.save(d));

    fakenvs::failWrite = false;
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(80, r.brightness);
}

static void test_sequence_wraps_around() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d, r;
    storage.loadAndUpgrade(d);
    setSequence("set_A", 0xFFFFFFFFu);

    d.brightness = 42;
    ASSERT_RESULT(R::OK, storage.save(d));
    TEST_ASSERT_EQUAL_UINT32(0, sequenceOf("set_B"));
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(42, r.brightness);

    d.brightness = 43;
    ASSERT_RESULT(R::OK, storage.save(d));
    TEST_ASSERT_EQUAL_UINT32(1, sequenceOf("set_A"));
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL(43, r.brightness);
}

static void test_erase_removes_both_slots() {
    StorageV2 storage("ns", "set", 1, defV2);
    V2 d;
    storage.loadAndUpgrade(d);
    d.brightness = 1;
    storage.save(d);

    TEST_ASSERT_TRUE(storage.erase());
    TEST_ASSERT_FALSE(hasKey("set_A"));
    TEST_ASSERT_FALSE(hasKey("set_B"));
    TEST_ASSERT_TRUE(storage.erase());               // already empty
    ASSERT_RESULT(R::DEFAULTS_LOADED, storage.load(d));
}

static void test_name_length_limits() {
    V2 d;
    StorageV2 longNamespace("0123456789abcdef", "set", 1, defV2);   // 16 chars
    StorageV2 longKey("ns", "0123456789abcd", 1, defV2);            // 14 chars
    StorageV2 maxNames("0123456789abcde", "0123456789abc", 1, defV2);

    ASSERT_RESULT(R::INVALID_ARGUMENT, longNamespace.load(d));
    ASSERT_RESULT(R::INVALID_ARGUMENT, longNamespace.save(d));
    TEST_ASSERT_FALSE(longNamespace.erase());
    ASSERT_RESULT(R::INVALID_ARGUMENT, longKey.loadAndUpgrade(d));

    ASSERT_RESULT(R::DEFAULTS_LOADED, maxNames.loadAndUpgrade(d));
    TEST_ASSERT_EQUAL(1, fakenvs::store["0123456789abcde"].count("0123456789abc_A"));
}

static void test_helpers() {
    StorageV2 storage("ns", "set", 7, defV2);
    TEST_ASSERT_EQUAL(7, storage.currentVersion());
    TEST_ASSERT_EQUAL_STRING("FUTURE_VERSION", StorageV2::resultToString(R::FUTURE_VERSION));
    TEST_ASSERT_TRUE(StorageV2::isSuccess(R::OK));
    TEST_ASSERT_TRUE(StorageV2::isSuccess(R::UNCHANGED));
    TEST_ASSERT_TRUE(StorageV2::isSuccess(R::DEFAULTS_LOADED));
    TEST_ASSERT_FALSE(StorageV2::isSuccess(R::VERIFY_ERROR));
}

static void test_on_flash_format_matches_original_implementation() {
    V2 d, r, l;
    memset(&d, 0, sizeof(d));
    defV2(d);
    d.brightness = 33;
    TEST_ASSERT_EQUAL(0, legacySaveV2("ns", "set", 1, d));
    d.brightness = 34;
    TEST_ASSERT_EQUAL(0, legacySaveV2("ns", "set", 1, d));    // A seq 1, B seq 2

    // Original -> current
    StorageV2 storage("ns", "set", 1, defV2);
    ASSERT_RESULT(R::OK, storage.load(r));
    TEST_ASSERT_EQUAL_MEMORY(&d, &r, sizeof(d));
    ASSERT_RESULT(R::UNCHANGED, storage.save(r));
    r.brightness = 35;
    ASSERT_RESULT(R::OK, storage.save(r));
    TEST_ASSERT_EQUAL_UINT32(3, sequenceOf("set_A"));

    // Current -> original
    TEST_ASSERT_EQUAL(0, legacyLoadV2("ns", "set", 1, l));
    TEST_ASSERT_EQUAL(35, l.brightness);
    r.brightness = 36;
    TEST_ASSERT_EQUAL(0, legacySaveV2("ns", "set", 1, r));
    TEST_ASSERT_EQUAL_UINT32(4, sequenceOf("set_B"));
    ASSERT_RESULT(R::OK, storage.load(l));
    TEST_ASSERT_EQUAL(36, l.brightness);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_boot_load_uses_defaults_without_writing);
    RUN_TEST(test_first_boot_load_and_upgrade_persists_defaults);
    RUN_TEST(test_save_alternates_slots_and_skips_unchanged_data);
    RUN_TEST(test_corrupted_newest_slot_falls_back_to_other_slot);
    RUN_TEST(test_upgrade_keeps_appended_defaults_and_migrates_once);
    RUN_TEST(test_load_migrates_old_version_in_ram_only);
    RUN_TEST(test_future_version_is_not_loaded_nor_overwritten);
    RUN_TEST(test_save_refuses_to_write_when_a_slot_is_unreadable);
    RUN_TEST(test_unreadable_slots_are_not_replaced_by_defaults);
    RUN_TEST(test_write_failure_keeps_previous_data);
    RUN_TEST(test_sequence_wraps_around);
    RUN_TEST(test_erase_removes_both_slots);
    RUN_TEST(test_name_length_limits);
    RUN_TEST(test_helpers);
    RUN_TEST(test_on_flash_format_matches_original_implementation);
    return UNITY_END();
}
