#pragma once

// This device's own settings and IoT credentials (BOOT-10), read at boot from the `prov` NVS
// partition that scripts/provision.sh writes. None of it is compiled into the firmware, so every
// catcam runs the same image and a published firmware.bin holds no secrets.
//
// See docs/provisioning.md for the layout and the keys.

#include <Arduino.h>

// Partition label and NVS namespace (both "prov"); must match partitions_custom_s3.csv and
// scripts/provision.sh.
#define PROV_PARTITION_LABEL "prov"
#define PROV_NAMESPACE "prov"

struct ProvisioningData {
    String thingName;           // "thing"          IoT Thing name; also the MQTT client id
    String wifiSsid;            // "wifi_ssid"
    String wifiPassword;        // "wifi_pass"
    String iotEndpoint;         // "iot_endpoint"   iot:Data-ATS endpoint (MQTT)
    String credentialsEndpoint; // "cred_endpoint"  iot:CredentialProvider endpoint
    String apiHost;             // "api_host"       API Gateway host for /infer and video upload
    String roleAlias;           // "role_alias"     optional, default BootBootsRoleAlias
    String region;              // "region"         optional, default eu-west-2
    String environment;         // "env"            optional, informational (sandbox / prod)
    String rootCa;              // "root_ca"        PEM (Amazon Root CA 1)
    String deviceCert;          // "cert"           PEM
    String deviceKey;           // "key"            PEM
};

class Provisioning {
public:
    static Provisioning& getInstance();

    // Reads the prov partition. Returns false, and remembers which keys are missing, if the
    // partition is absent, empty or incomplete. Safe to call more than once.
    bool load();

    bool isProvisioned() const { return _provisioned; }

    // Valid for the life of the program; the c_str() pointers stay put once load() returns, so
    // they may be handed to WiFiClientSecure / MqttService, which keep the pointers.
    const ProvisioningData& data() const { return _data; }

    // Why load() failed, for the log: "no prov partition", or the list of missing keys.
    const String& problem() const { return _problem; }

private:
    Provisioning() = default;
    ProvisioningData _data;
    bool _provisioned = false;
    String _problem;
};
