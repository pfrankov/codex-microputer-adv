// Runtime discovery for the loader-owned/shared NVS partition. Prefer the
// `apps_nvs` label used by M5Apps, then fall back to any partition whose
// DATA/NVS type and subtype satisfy the stable loader contract.
#pragma once

#include "esp_partition.h"

namespace storage_partition {

inline const esp_partition_t* nvs()
{
    static const esp_partition_t* partition = [] {
        const esp_partition_t* preferred = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "apps_nvs");
        return preferred ? preferred : esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    }();
    return partition;
}

inline const char* nvs_label()
{
    const esp_partition_t* partition = nvs();
    return partition ? partition->label : nullptr;
}

}  // namespace storage_partition
