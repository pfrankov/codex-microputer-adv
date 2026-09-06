#include <cstdio>
#include <cstring>
#include "esp_err.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "nvs.h"
#include "storage_partition.h"
namespace {
constexpr char kTag[] = "ccp-bond", kNamespace[] = "codex_ccp2";
constexpr char kKeyOurSec[] = "ble_our", kKeyPeerSec[] = "ble_peer", kKeyCccd[] = "ble_cccd",
               kSchemaKey[] = "schema";
constexpr uint8_t kSchemaVersion = 3;
constexpr int kMaxBonds = 8, kMaxCccds = 8;
ble_store_value_sec our_secs[kMaxBonds], peer_secs[kMaxBonds];
ble_store_value_cccd cccds[kMaxCccds];
int our_sec_count = 0, peer_sec_count = 0, cccd_count = 0;
esp_err_t persist(const char* key, const void* data, size_t size, int count)
{
    nvs_handle_t h = 0;
    const char* partition = storage_partition::nvs_label();
    esp_err_t e = partition
        ? nvs_open_from_partition(partition, kNamespace, NVS_READWRITE, &h)
        : ESP_ERR_NOT_FOUND;
    if (e != ESP_OK)
        return e;
    char count_key[16];
    std::snprintf(count_key, sizeof(count_key), "%s_n", key);
    e = nvs_set_u8(h, count_key, static_cast<uint8_t>(count));
    if (e == ESP_OK) {
        e = count > 0 ? nvs_set_blob(h, key, data, size * count) : nvs_erase_key(h, key);
        if (count == 0 && e == ESP_ERR_NVS_NOT_FOUND)
            e = ESP_OK;
    }
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK)
        ESP_LOGE(kTag, "persist %s failed: %s", key, esp_err_to_name(e));
    return e;
}
void restore(const char* key, void* data, size_t size, int max_count, int* count)
{
    *count = 0;
    nvs_handle_t h = 0;
    const char* partition = storage_partition::nvs_label();
    if (!partition
        || nvs_open_from_partition(partition, kNamespace, NVS_READONLY, &h) != ESP_OK)
        return;
    char count_key[16];
    std::snprintf(count_key, sizeof(count_key), "%s_n", key);
    uint8_t stored = 0;
    if (nvs_get_u8(h, count_key, &stored) == ESP_OK && stored > 0) {
        if (stored > max_count)
            ESP_LOGW(kTag, "%s count %u exceeds capacity", key, (unsigned)stored);
        else {
            size_t len = size * stored;
            if (nvs_get_blob(h, key, data, &len) == ESP_OK && len == size * stored)
                *count = stored;
            else
                ESP_LOGW(kTag, "%s blob malformed", key);
        }
    }
    nvs_close(h);
}
bool keys_off_peer(const ble_addr_t& a)
{
    static const ble_addr_t none = {};
    return std::memcmp(&a, &none, sizeof(a)) != 0;
}
int find_sec(const ble_store_key_sec* key, const ble_store_value_sec* values, int count)
{
    int skipped = 0;
    for (int i = 0; i < count; ++i) {
        if (keys_off_peer(key->peer_addr) &&
            ble_addr_cmp(&values[i].peer_addr, &key->peer_addr) != 0)
            continue;
        if (key->idx > skipped) {
            ++skipped;
            continue;
        }
        return i;
    }
    return -1;
}
int find_cccd(const ble_store_key_cccd* key, const ble_store_value_cccd* values, int count)
{
    int skipped = 0;
    for (int i = 0; i < count; ++i) {
        if (keys_off_peer(key->peer_addr) &&
            ble_addr_cmp(&values[i].peer_addr, &key->peer_addr) != 0)
            continue;
        if (key->chr_val_handle != 0 && values[i].chr_val_handle != key->chr_val_handle)
            continue;
        if (key->idx > skipped) {
            ++skipped;
            continue;
        }
        return i;
    }
    return -1;
}
int find_cccd(const ble_store_key_cccd* key)
{
    return find_cccd(key, cccds, cccd_count);
}
int write_sec(bool peer, const ble_store_value_sec* value)
{
    auto* values = peer ? peer_secs : our_secs;
    int* count = peer ? &peer_sec_count : &our_sec_count;
    ble_store_value_sec candidate[kMaxBonds] = {};
    std::memcpy(candidate, values, sizeof(candidate));
    ble_store_key_sec key = {};
    ble_store_key_from_value_sec(&key, value);
    int index = find_sec(&key, candidate, *count), next = *count;
    if (index < 0) {
        if (next >= kMaxBonds)
            return BLE_HS_ESTORE_CAP;
        index = next++;
    }
    candidate[index] = *value;
    if (persist(peer ? kKeyPeerSec : kKeyOurSec, candidate, sizeof(*candidate), next) != ESP_OK)
        return BLE_HS_ESTORE_FAIL;
    std::memcpy(values, candidate, sizeof(candidate));
    *count = next;
    return 0;
}
int delete_sec(bool peer, const ble_store_key_sec* key)
{
    auto* values = peer ? peer_secs : our_secs;
    int* count = peer ? &peer_sec_count : &our_sec_count;
    int index = find_sec(key, values, *count);
    if (index < 0)
        return BLE_HS_ENOENT;
    ble_store_value_sec candidate[kMaxBonds] = {};
    std::memcpy(candidate, values, sizeof(candidate));
    std::memmove(&candidate[index], &candidate[index + 1],
                 sizeof(*candidate) * (*count - index - 1));
    int next = *count - 1;
    if (persist(peer ? kKeyPeerSec : kKeyOurSec, candidate, sizeof(*candidate), next) != ESP_OK)
        return BLE_HS_ESTORE_FAIL;
    std::memcpy(values, candidate, sizeof(candidate));
    *count = next;
    return 0;
}
int store_read(int type, const ble_store_key* key, ble_store_value* value)
{
    switch (type) {
    case BLE_STORE_OBJ_TYPE_OUR_SEC:
    case BLE_STORE_OBJ_TYPE_PEER_SEC: {
        bool peer = type == BLE_STORE_OBJ_TYPE_PEER_SEC;
        int i =
            find_sec(&key->sec, peer ? peer_secs : our_secs, peer ? peer_sec_count : our_sec_count);
        if (i < 0)
            return BLE_HS_ENOENT;
        value->sec = peer ? peer_secs[i] : our_secs[i];
        return 0;
    }
    case BLE_STORE_OBJ_TYPE_CCCD: {
        int i = find_cccd(&key->cccd);
        if (i < 0)
            return BLE_HS_ENOENT;
        value->cccd = cccds[i];
        return 0;
    }
    default:
        return BLE_HS_ENOTSUP;
    }
}
int store_write(int type, const ble_store_value* value)
{
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC)
        return write_sec(false, &value->sec);
    if (type == BLE_STORE_OBJ_TYPE_PEER_SEC)
        return write_sec(true, &value->sec);
    if (type != BLE_STORE_OBJ_TYPE_CCCD)
        return BLE_HS_ENOTSUP;
    ble_store_value_cccd candidate[kMaxCccds] = {};
    std::memcpy(candidate, cccds, sizeof(candidate));
    ble_store_key_cccd key = {};
    ble_store_key_from_value_cccd(&key, &value->cccd);
    int i = find_cccd(&key, candidate, cccd_count), next = cccd_count;
    if (i < 0) {
        if (next >= kMaxCccds)
            return BLE_HS_ESTORE_CAP;
        i = next++;
    }
    candidate[i] = value->cccd;
    if (persist(kKeyCccd, candidate, sizeof(*candidate), next) != ESP_OK)
        return BLE_HS_ESTORE_FAIL;
    std::memcpy(cccds, candidate, sizeof(candidate));
    cccd_count = next;
    return 0;
}
int store_delete(int type, const ble_store_key* key)
{
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC)
        return delete_sec(false, &key->sec);
    if (type == BLE_STORE_OBJ_TYPE_PEER_SEC)
        return delete_sec(true, &key->sec);
    if (type != BLE_STORE_OBJ_TYPE_CCCD)
        return BLE_HS_ENOTSUP;
    int i = find_cccd(&key->cccd);
    if (i < 0)
        return BLE_HS_ENOENT;
    ble_store_value_cccd candidate[kMaxCccds] = {};
    std::memcpy(candidate, cccds, sizeof(candidate));
    std::memmove(&candidate[i], &candidate[i + 1], sizeof(*candidate) * (cccd_count - i - 1));
    int next = cccd_count - 1;
    if (persist(kKeyCccd, candidate, sizeof(*candidate), next) != ESP_OK)
        return BLE_HS_ESTORE_FAIL;
    std::memcpy(cccds, candidate, sizeof(candidate));
    cccd_count = next;
    return 0;
}
esp_err_t erase_bond_keys(nvs_handle_t h)
{
    static const char* const keys[] = {
        kKeyOurSec, "ble_our_n", kKeyPeerSec, "ble_peer_n", kKeyCccd, "ble_cccd_n",
    };
    for (const char* key : keys) {
        const esp_err_t e = nvs_erase_key(h, key);
        if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(kTag, "erase %s failed: %s", key, esp_err_to_name(e));
            return e;
        }
    }
    return ESP_OK;
}
} // namespace
extern "C" void companion_ble_store_init(void)
{
    nvs_handle_t h = 0;
    const char* partition = storage_partition::nvs_label();
    const esp_err_t open_error = partition
        ? nvs_open_from_partition(partition, kNamespace, NVS_READWRITE, &h)
        : ESP_ERR_NOT_FOUND;
    if (open_error == ESP_OK) {
        uint8_t schema = 0;
        if (nvs_get_u8(h, kSchemaKey, &schema) != ESP_OK || schema != kSchemaVersion) {
            esp_err_t e = erase_bond_keys(h);
            if (e == ESP_OK)
                e = nvs_set_u8(h, kSchemaKey, kSchemaVersion);
            if (e == ESP_OK)
                e = nvs_commit(h);
            if (e == ESP_OK)
                ESP_LOGI(kTag, "cleared legacy BLE bonds");
            else
                ESP_LOGE(kTag, "schema migration failed: %s", esp_err_to_name(e));
        }
        nvs_close(h);
    } else
        ESP_LOGE(kTag, "open failed: %s", esp_err_to_name(open_error));
    restore(kKeyOurSec, our_secs, sizeof(*our_secs), kMaxBonds, &our_sec_count);
    restore(kKeyPeerSec, peer_secs, sizeof(*peer_secs), kMaxBonds, &peer_sec_count);
    restore(kKeyCccd, cccds, sizeof(*cccds), kMaxCccds, &cccd_count);
    ESP_LOGI(kTag, "restored %d bonds, %d cccds", peer_sec_count, cccd_count);
    ble_hs_cfg.store_read_cb = store_read;
    ble_hs_cfg.store_write_cb = store_write;
    ble_hs_cfg.store_delete_cb = store_delete;
}
extern "C" int companion_ble_store_bond_count(void)
{
    return peer_sec_count;
}
extern "C" esp_err_t companion_ble_store_forget_all(void)
{
    nvs_handle_t h = 0;
    const char* partition = storage_partition::nvs_label();
    esp_err_t e = partition
        ? nvs_open_from_partition(partition, kNamespace, NVS_READWRITE, &h)
        : ESP_ERR_NOT_FOUND;
    if (e != ESP_OK) {
        ESP_LOGE(kTag, "reset open failed: %s", esp_err_to_name(e));
        return e;
    }
    e = erase_bond_keys(h);
    if (e == ESP_OK)
        e = nvs_set_u8(h, kSchemaKey, kSchemaVersion);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGE(kTag, "reset failed: %s", esp_err_to_name(e));
        return e;
    }
    std::memset(our_secs, 0, sizeof(our_secs));
    std::memset(peer_secs, 0, sizeof(peer_secs));
    std::memset(cccds, 0, sizeof(cccds));
    our_sec_count = 0;
    peer_sec_count = 0;
    cccd_count = 0;
    ESP_LOGI(kTag, "reset complete; settings namespace retained");
    return ESP_OK;
}
