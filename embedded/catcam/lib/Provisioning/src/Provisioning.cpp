#include "Provisioning.h"

#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

Provisioning& Provisioning::getInstance() {
    static Provisioning instance;
    return instance;
}

// Reads one string; an absent key leaves `out` empty and returns false.
static bool readString(nvs_handle_t handle, const char* key, String& out) {
    size_t len = 0;
    if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK || len == 0) {
        return false;
    }
    char* buf = (char*)malloc(len);
    if (!buf) {
        return false;
    }
    bool ok = nvs_get_str(handle, key, buf, &len) == ESP_OK;
    if (ok) {
        out = String(buf);
    }
    // The buffer may hold a private key or a password.
    memset(buf, 0, len);
    free(buf);
    return ok && out.length() > 0;
}

bool Provisioning::load() {
    if (_provisioned) {
        return true;
    }
    _problem = "";

    if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, PROV_PARTITION_LABEL)) {
        _problem = "no '" PROV_PARTITION_LABEL "' partition in the partition table (flash the BOOT-10 partition table)";
        return false;
    }

    esp_err_t err = nvs_flash_init_partition(PROV_PARTITION_LABEL);
    if (err != ESP_OK) {
        _problem = String("the '" PROV_PARTITION_LABEL "' partition isn't a readable NVS image (")
                   + esp_err_to_name(err) + "): run scripts/provision.sh";
        return false;
    }

    nvs_handle_t handle;
    err = nvs_open_from_partition(PROV_PARTITION_LABEL, PROV_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        nvs_flash_deinit_partition(PROV_PARTITION_LABEL);
        _problem = String("the '" PROV_PARTITION_LABEL "' partition has no '" PROV_NAMESPACE "' namespace (")
                   + esp_err_to_name(err) + "): run scripts/provision.sh";
        return false;
    }

    struct Field { const char* key; String* value; bool required; };
    const Field fields[] = {
        {"thing",         &_data.thingName,           true},
        {"wifi_ssid",     &_data.wifiSsid,            true},
        {"wifi_pass",     &_data.wifiPassword,        true},
        {"iot_endpoint",  &_data.iotEndpoint,         true},
        {"cred_endpoint", &_data.credentialsEndpoint, true},
        {"api_host",      &_data.apiHost,             true},
        {"root_ca",       &_data.rootCa,              true},
        {"cert",          &_data.deviceCert,          true},
        {"key",           &_data.deviceKey,           true},
        {"role_alias",    &_data.roleAlias,           false},
        {"region",        &_data.region,              false},
        {"env",           &_data.environment,         false},
    };

    String missing;
    for (const Field& f : fields) {
        if (!readString(handle, f.key, *f.value) && f.required) {
            missing += missing.length() ? ", " : "";
            missing += f.key;
        }
    }
    nvs_close(handle);
    nvs_flash_deinit_partition(PROV_PARTITION_LABEL);

    if (_data.roleAlias.length() == 0) _data.roleAlias = "BootBootsRoleAlias";
    if (_data.region.length() == 0) _data.region = "eu-west-2";

    if (missing.length()) {
        _problem = "the '" PROV_PARTITION_LABEL "' partition lacks: " + missing + " (run scripts/provision.sh)";
        return false;
    }

    _provisioned = true;
    return true;
}
