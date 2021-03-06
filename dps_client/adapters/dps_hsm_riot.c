// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include "azure_c_shared_utility/umock_c_prod.h"
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/urlencode.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/crt_abstractions.h"

#include "dps_hsm_riot.h"

#include "RIoT.h"
#include "RiotCrypt.h"
#include "RiotDerEnc.h"
#include "RiotX509Bldr.h"
#include "DiceSha256.h"

#define RIOT_SIGNER_NAME            "riot-signer-core"
#define RIOT_COMMON_NAME            "riot-device-cert"
#define RIOT_CA_CERT_NAME           "riot-root"

// Note that even though digest lengths are equivalent here, (and on most
// devices this will be the case) there is no requirement that DICE and RIoT
// use the same one-way function/digest length.
#define DICE_DIGEST_LENGTH          RIOT_DIGEST_LENGTH

// Note also that there is no requirement on the UDS length for a device.
// A 256-bit UDS is recommended but this size may vary among devices.
#define DICE_UDS_LENGTH             0x20

// Size, in bytes, returned when the required certificate buffer size is
// requested.  For this emulator the actual size (~552 bytes) is static,
// based on the contents of the x509TBSData struct (the fiels don't vary).
// As x509 data varies so will, obviously, the overall cert length. For now,
// just pick a reasonable minimum buffer size and worry about this later.
#define REASONABLE_MIN_CERT_SIZE    DER_MAX_TBS

// Emulator specific
// Random (i.e., simulated) RIoT Core "measurement"
static uint8_t RAMDOM_DIGEST[DICE_DIGEST_LENGTH] = {
    0xb5, 0x85, 0x94, 0x93, 0x66, 0x1e, 0x2e, 0xae,
    0x96, 0x77, 0xc5, 0x5d, 0x59, 0x0b, 0x92, 0x94,
    0xe0, 0x94, 0xab, 0xaf, 0xd7, 0x40, 0x78, 0x7e,
    0x05, 0x0d, 0xfe, 0x6d, 0x85, 0x90, 0x53, 0xa0 };

// The static data fields that make up the x509 "to be signed" region
//static RIOT_X509_TBS_DATA X509_TBS_DATA = { { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E },
//"RIoT Core", "MSR_TEST", "US", "170101000000Z", "370101000000Z", "RIoT Device", "MSR_TEST", "US" };

// The static data fields that make up the Alias Cert "to be signed" region
static RIOT_X509_TBS_DATA X509_ALIAS_TBS_DATA = {
    { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E }, RIOT_SIGNER_NAME, "MSR_TEST", "US",
    "170101000000Z", "370101000000Z", RIOT_COMMON_NAME, "MSR_TEST", "US" };

// The static data fields that make up the DeviceID Cert "to be signed" region
static RIOT_X509_TBS_DATA X509_DEVICE_TBS_DATA = { 
    { 0x0E, 0x0D, 0x0C, 0x0B, 0x0A }, RIOT_CA_CERT_NAME, "MSR_TEST", "US",
    "170101000000Z", "370101000000Z", RIOT_SIGNER_NAME, "MSR_TEST", "US" };

// The static data fields that make up the "root signer" Cert
static RIOT_X509_TBS_DATA X509_ROOT_TBS_DATA = {
    { 0x1A, 0x2B, 0x3C, 0x4D, 0x5E }, RIOT_CA_CERT_NAME, "MSR_TEST", "US",
    "170101000000Z", "370101000000Z", RIOT_CA_CERT_NAME, "MSR_TEST", "US" };

#define DER_ECC_KEY_MAX     0x80
#define DER_ECC_PUB_MAX     0x60

static int g_digest_initialized = 0;
static uint8_t g_digest[DICE_DIGEST_LENGTH] = { 0 };
static unsigned char g_uds_seed[DICE_UDS_LENGTH] = { 
    0x54, 0x10, 0x5D, 0x2E, 0xCD, 0x07, 0xF9, 0x01, 
    0x99, 0xB3, 0x95, 0xC7, 0x42, 0x61, 0xA0, 0x8C, 
    0xFF, 0x27, 0x1A, 0x0D, 0xF6, 0x6F, 0x1F, 0xE0,
    0x00, 0x34, 0xBB, 0x11, 0xF7, 0x98, 0x9A, 0x12 };
static uint8_t g_CDI[DICE_DIGEST_LENGTH] = { 
    0x91, 0x75, 0xDB, 0xEE, 0x90, 0xC4, 0xE1, 0xE3,
    0x74, 0x47, 0x2C, 0x8A, 0x55, 0x3F, 0xD2, 0xB8, 
    0xE9, 0x79, 0xEE, 0xF1, 0x62, 0xF8, 0x64, 0xDA, 
    0x50, 0x69, 0x4B, 0x3E, 0x5A, 0x1E, 0x3A, 0x6E };

// The "root" signing key. This is intended for development purposes only.
// This key is used to sign the DeviceID certificate, the certificiate for
// this "root" key represents the "trusted" CA for the developer-mode DPS
// server(s). Again, this is for development purposes only and (obviously)
// provides no meaningful security whatsoever.
static unsigned char eccRootPubBytes[sizeof(ecc_publickey)] = {
    0xeb, 0x9c, 0xfc, 0xc8, 0x49, 0x94, 0xd3, 0x50, 0xa7, 0x1f, 0x9d, 0xc5,
    0x09, 0x3d, 0xd2, 0xfe, 0xb9, 0x48, 0x97, 0xf4, 0x95, 0xa5, 0x5d, 0xec,
    0xc9, 0x0f, 0x52, 0xa1, 0x26, 0x5a, 0xab, 0x69, 0x00, 0x00, 0x00, 0x00,
    0x7d, 0xce, 0xb1, 0x62, 0x39, 0xf8, 0x3c, 0xd5, 0x9a, 0xad, 0x9e, 0x05,
    0xb1, 0x4f, 0x70, 0xa2, 0xfa, 0xd4, 0xfb, 0x04, 0xe5, 0x37, 0xd2, 0x63,
    0x9a, 0x46, 0x9e, 0xfd, 0xb0, 0x5b, 0x1e, 0xdf, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00 };

static unsigned char eccRootPrivBytes[sizeof(ecc_privatekey)] = {
    0xe3, 0xe7, 0xc7, 0x13, 0x57, 0x3f, 0xd9, 0xc8, 0xb8, 0xe1, 0xea, 0xf4,
    0x53, 0xf1, 0x56, 0x15, 0x02, 0xf0, 0x71, 0xc0, 0x53, 0x49, 0xc8, 0xda,
    0xe6, 0x26, 0xa9, 0x0b, 0x17, 0x88, 0xe5, 0x70, 0x00, 0x00, 0x00, 0x00 };

typedef enum CERTIFICATE_SIGNING_TYPE_TAG
{
    type_self_sign,
    type_riot_csr,
    type_root_signed
} CERTIFICATE_SIGNING_TYPE;

typedef struct DPS_SECURE_DEVICE_INFO_TAG
{
    // In Riot these are call the device Id pub and pri
    RIOT_ECC_PUBLIC     device_id_pub; 
    RIOT_ECC_PRIVATE    device_id_priv;

    RIOT_ECC_PUBLIC     alias_key_pub;
    RIOT_ECC_PRIVATE    alias_key_priv;

    RIOT_ECC_PUBLIC     ca_root_pub;
    RIOT_ECC_PRIVATE    ca_root_priv;

    char* certificate_common_name;

    uint32_t device_id_length;
    char device_id_public_pem[DER_MAX_PEM];

    uint32_t device_signed_length;
    char device_signed_pem[DER_MAX_PEM];

    uint32_t alias_key_length;
    char alias_priv_key_pem[DER_MAX_PEM];

    uint32_t alias_cert_length;
    char alias_cert_pem[DER_MAX_PEM];

    uint32_t root_ca_length;
    char root_ca_pem[DER_MAX_PEM];

    uint32_t root_ca_priv_length;
    char root_ca_priv_pem[DER_MAX_PEM];

} DPS_SECURE_DEVICE_INFO;

static const SEC_X509_INTERFACE sec_riot_interface =
{
    dps_hsm_riot_create,
    dps_hsm_riot_destroy,
    dps_hsm_riot_get_certificate,
    dps_hsm_riot_get_alias_key,
    dps_hsm_riot_get_signer_cert,
    dps_hsm_riot_get_root_cert,
    dps_hsm_riot_get_root_key,
    dps_hsm_riot_get_common_name
};

static char* convert_key_to_string(const unsigned char* key_value, size_t key_length)
{
    char* result;

    result = malloc((key_length*2)+1);
    memset(result, 0, (key_length*2)+1);

    char hex_val[3];
    for (size_t index = 0; index < key_length; index++)
    {
        sprintf(hex_val, "%02x", key_value[index]);
        strcat(result, hex_val);
    }
    return result;
}

static int produce_priv_key(DPS_SECURE_DEVICE_INFO* riot_info)
{
    int result;
    uint8_t der_buffer[DER_MAX_TBS] = { 0 };
    DERBuilderContext der_ctx = { 0 };

    memcpy(&riot_info->ca_root_pub, eccRootPubBytes, sizeof(ecc_publickey));
    memcpy(&riot_info->ca_root_priv, eccRootPrivBytes, sizeof(ecc_privatekey));

    DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
    if (X509GetDEREcc(&der_ctx, riot_info->ca_root_pub, riot_info->ca_root_priv) != 0)
    {
        LogError("Failure: X509GetDEREcc returned invalid status.");
        result = __FAILURE__;
    }
    else
    {
        riot_info->root_ca_priv_length = sizeof(riot_info->root_ca_priv_pem);
        if (DERtoPEM(&der_ctx, ECC_PRIVATEKEY_TYPE, riot_info->root_ca_priv_pem, &riot_info->root_ca_priv_length) != 0)
        {
            LogError("Failure: DERtoPEM returned invalid status.");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}

static int produce_root_ca(DPS_SECURE_DEVICE_INFO* riot_info, RIOT_ECC_SIGNATURE tbs_sig)
{
    int result;
    uint8_t der_buffer[DER_MAX_TBS] = { 0 };
    DERBuilderContext der_ctx = { 0 };
    RIOT_STATUS status;

    // Generating "root"-signed DeviceID certificate
    DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
    if (X509GetDeviceCertTBS(&der_ctx, &X509_ROOT_TBS_DATA, (RIOT_ECC_PUBLIC*)&eccRootPubBytes) != 0)
    {
        LogError("Failure: X509GetDeviceCertTBS");
        result = __FAILURE__;
    }
    // Sign the DeviceID Certificate's TBS region
    else if ((status = RiotCrypt_Sign(&tbs_sig, der_ctx.Buffer, der_ctx.Position, &riot_info->device_id_priv)) != RIOT_SUCCESS)
    {
        LogError("Failure: RiotCrypt_Sign returned invalid status %d.", status);
        result = __FAILURE__;
    }
    else if (X509MakeRootCert(&der_ctx, &tbs_sig) != 0)
    {
        LogError("Failure: X509MakeRootCert");
        result = __FAILURE__;
    }
    else
    {
        riot_info->root_ca_length = sizeof(riot_info->root_ca_pem);
        if (DERtoPEM(&der_ctx, CERT_TYPE, riot_info->root_ca_pem, &riot_info->root_ca_length) != 0)
        {
            LogError("Failure: DERtoPEM return invalid value.");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}

static int produce_device_cert(DPS_SECURE_DEVICE_INFO* riot_info, RIOT_ECC_SIGNATURE tbs_sig, CERTIFICATE_SIGNING_TYPE signing_type)
{
    int result;
    uint8_t der_buffer[DER_MAX_TBS] = { 0 };
    DERBuilderContext der_ctx = { 0 };
    RIOT_STATUS status;

    if (signing_type == type_self_sign)
    {
        // Build the TBS (to be signed) region of DeviceID Certificate
        DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
        if (X509GetDeviceCertTBS(&der_ctx, &X509_DEVICE_TBS_DATA, &riot_info->device_id_pub) != 0)
        {
            LogError("Failure: X509GetDeviceCertTBS");
            result = __FAILURE__;
        }
        // Sign the DeviceID Certificate's TBS region
        else if ((status = RiotCrypt_Sign(&tbs_sig, der_ctx.Buffer, der_ctx.Position, &riot_info->device_id_priv)) != RIOT_SUCCESS)
        {
            LogError("Failure: RiotCrypt_Sign returned invalid status %d.", status);
            result = __FAILURE__;
        }
        else if (X509MakeDeviceCert(&der_ctx, &tbs_sig) != 0)
        {
            LogError("Failure: X509MakeDeviceCert");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    else if (signing_type == type_riot_csr)
    {
        DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
        if (X509GetDERCsrTbs(&der_ctx, &X509_ALIAS_TBS_DATA, &riot_info->device_id_pub) != 0)
        {
            LogError("Failure: X509GetDeviceCertTBS");
            result = __FAILURE__;
        }
        // Sign the Alias Key Certificate's TBS region
        else if ((status = RiotCrypt_Sign(&tbs_sig, der_ctx.Buffer, der_ctx.Position, &riot_info->device_id_priv)) == RIOT_SUCCESS)
        {
            LogError("Failure: RiotCrypt_Sign returned invalid status %d.", status);
            result = __FAILURE__;
        }
        // Create CSR for DeviceID
        else if (X509GetDERCsr(&der_ctx, &tbs_sig) != 0)
        {
            LogError("Failure: X509GetDERCsr");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    else
    {
        // Generating "root"-signed DeviceID certificate
        DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
        if (X509GetDeviceCertTBS(&der_ctx, &X509_DEVICE_TBS_DATA, &riot_info->device_id_pub) != 0)
        {
            LogError("Failure: X509GetDeviceCertTBS");
            result = __FAILURE__;
        }
        // Sign the DeviceID Certificate's TBS region
        else if ((status = RiotCrypt_Sign(&tbs_sig, der_ctx.Buffer, der_ctx.Position, (RIOT_ECC_PRIVATE*)eccRootPrivBytes)) != RIOT_SUCCESS)
        {
            LogError("Failure: RiotCrypt_Sign returned invalid status %d.", status);
            result = __FAILURE__;
        }
        else if (X509MakeDeviceCert(&der_ctx, &tbs_sig) != 0)
        {
            LogError("Failure: X509MakeDeviceCert");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }

    if (result == 0)
    {
        riot_info->device_signed_length = sizeof(riot_info->device_signed_pem);
        if (DERtoPEM(&der_ctx, CERT_TYPE, riot_info->device_signed_pem, &riot_info->device_signed_length) != 0)
        {
            LogError("Failure: DERtoPEM return invalid value.");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}

static int produce_alias_key_cert(DPS_SECURE_DEVICE_INFO* riot_info, DERBuilderContext* cert_ctx)
{
    int result;
    riot_info->alias_cert_length = sizeof(riot_info->alias_cert_pem);
    if (DERtoPEM(cert_ctx, CERT_TYPE, riot_info->alias_cert_pem, &riot_info->alias_cert_length) != 0)
    {
        LogError("Failure: DERtoPEM return invalid value.");
        result = __FAILURE__;
    }
    else
    {
        result = 0;
    }
    return result;
}

static int produce_alias_key_pair(DPS_SECURE_DEVICE_INFO* riot_info)
{
    int result;
    uint8_t der_buffer[DER_MAX_TBS] = { 0 };
    DERBuilderContext der_ctx = { 0 };

    DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
    if (X509GetDEREcc(&der_ctx, riot_info->alias_key_pub, riot_info->alias_key_priv) != 0)
    {
        LogError("Failure: X509GetDEREcc returned invalid status.");
        result = __FAILURE__;
    }
    else
    {
        riot_info->alias_key_length = sizeof(riot_info->alias_priv_key_pem);
        if (DERtoPEM(&der_ctx, ECC_PRIVATEKEY_TYPE, riot_info->alias_priv_key_pem, &riot_info->alias_key_length) != 0)
        {
            LogError("Failure: DERtoPEM returned invalid status.");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}

static int produce_device_id_public(DPS_SECURE_DEVICE_INFO* riot_info)
{
    int result;
    uint8_t der_buffer[DER_MAX_TBS] = { 0 };
    DERBuilderContext der_ctx = { 0 };

    // Copy DeviceID Public
    DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
    if (X509GetDEREccPub(&der_ctx, riot_info->device_id_pub) != 0)
    {
        LogError("Failure: X509GetDEREccPub returned invalid status.");
        result = __FAILURE__;
    }
    else
    {
        riot_info->device_id_length = sizeof(riot_info->device_id_public_pem);
        if (DERtoPEM(&der_ctx, PUBLICKEY_TYPE, riot_info->device_id_public_pem, &riot_info->device_id_length) != 0)
        {
            LogError("Failure: DERtoPEM returned invalid status.");
            result = __FAILURE__;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}

static int process_riot_key_info(DPS_SECURE_DEVICE_INFO* riot_info)
{
    int result;
    RIOT_STATUS status;

    unsigned char firmware_id[RIOT_DIGEST_LENGTH] = {
        0x6B, 0xE9, 0xB1, 0x84, 0xC9, 0x37, 0xC2, 0x8E,
        0x12, 0x2E, 0xEE, 0x51, 0x2B, 0x68, 0xEA, 0x8E,
        0x00, 0xC3, 0xDD, 0x15, 0x9E, 0xA4, 0xE8, 0x5E,
        0x84, 0xCB, 0xA9, 0x66, 0xF4, 0x46, 0xCD, 0x4E };

    /* Codes_SRS_SECURE_DEVICE_RIOT_07_002: [ dps_hsm_riot_create shall call into the RIot code to sign the RIoT certificate. ] */
    // Don't use CDI directly
    if (g_digest_initialized == 0)
    {
        LogError("Failure: secure_device_init was not called.");
        result = __FAILURE__;
    }
    else if (X509_ALIAS_TBS_DATA.SubjectCommon == NULL || strlen(X509_ALIAS_TBS_DATA.SubjectCommon) == 0)
    {
        LogError("Failure: The AX509_ALIAS_TBS_DATA.SubjectCommon is not entered");
        result = __FAILURE__;
    }
    else if ((status = RiotCrypt_Hash(g_digest, RIOT_DIGEST_LENGTH, g_CDI, DICE_DIGEST_LENGTH)) != RIOT_SUCCESS)
    {
        LogError("Failure: RiotCrypt_Hash returned invalid status %d.", status);
        result = __FAILURE__;
    }
    else if ((status = RiotCrypt_DeriveEccKey(&riot_info->device_id_pub, &riot_info->device_id_priv,
        g_digest, DICE_DIGEST_LENGTH, (const uint8_t*)RIOT_LABEL_IDENTITY, lblSize(RIOT_LABEL_IDENTITY))) != RIOT_SUCCESS)
    {
        LogError("Failure: RiotCrypt_DeriveEccKey returned invalid status %d.", status);
        result = __FAILURE__;
    }
    // Combine CDI and FWID, result in digest
    else if ((status = RiotCrypt_Hash2(g_digest, DICE_DIGEST_LENGTH, g_digest, DICE_DIGEST_LENGTH, firmware_id, RIOT_DIGEST_LENGTH)) != RIOT_SUCCESS)
    {
        LogError("Failure: RiotCrypt_Hash2 returned invalid status %d.", status);
        result = __FAILURE__;
    }
    // Derive Alias key pair from CDI and FWID
    else if ((status = RiotCrypt_DeriveEccKey(&riot_info->alias_key_pub, &riot_info->alias_key_priv,
        g_digest, RIOT_DIGEST_LENGTH, (const uint8_t*)RIOT_LABEL_ALIAS, lblSize(RIOT_LABEL_ALIAS))) != RIOT_SUCCESS)
    {
        LogError("Failure: RiotCrypt_DeriveEccKey returned invalid status %d.", status);
        result = __FAILURE__;
    }
    else
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_003: [ dps_hsm_riot_create shall cache the device id public value from the RIoT module. ] */
        if (produce_device_id_public(riot_info) != 0)
        {
            LogError("Failure: produce_device_id_public returned invalid result.");
            result = __FAILURE__;
        }
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_004: [ dps_hsm_riot_create shall cache the alias key pair value from the RIoT module. ] */
        else if (produce_alias_key_pair(riot_info) != 0)
        {
            LogError("Failure: produce_alias_key_pair returned invalid result.");
            result = __FAILURE__;
        }
        else
        {
            DERBuilderContext cert_ctx = { 0 };
            uint8_t cert_buffer[DER_MAX_TBS] = { 0 };
            RIOT_ECC_SIGNATURE tbs_sig = { 0 };

            /* Codes_SRS_SECURE_DEVICE_RIOT_07_005: [ dps_hsm_riot_create shall create the Signer regions of the alias key certificate. ]*/
            // Build the TBS (to be signed) region of Alias Key Certificate
            DERInitContext(&cert_ctx, cert_buffer, DER_MAX_TBS);
            if (X509GetAliasCertTBS(&cert_ctx, &X509_ALIAS_TBS_DATA, &riot_info->alias_key_pub, &riot_info->device_id_pub,
                firmware_id, RIOT_DIGEST_LENGTH) != 0)
            {
                LogError("Failure: X509GetAliasCertTBS returned invalid status.");
                result = __FAILURE__;
            }
            else if ((status = RiotCrypt_Sign(&tbs_sig, cert_ctx.Buffer, cert_ctx.Position, &riot_info->device_id_priv)) != RIOT_SUCCESS)
            {
                LogError("Failure: RiotCrypt_Sign returned invalid status %d.", status);
                result = __FAILURE__;
            }
            else if (X509MakeAliasCert(&cert_ctx, &tbs_sig) != 0)
            {
                LogError("Failure: X509MakeAliasCert returned invalid status.");
                result = __FAILURE__;
            }
            else if (produce_alias_key_cert(riot_info, &cert_ctx) != 0)
            {
                LogError("Failure: producing alias cert.");
                result = __FAILURE__;
            }
            else if (produce_device_cert(riot_info, tbs_sig, type_root_signed) != 0)
            {
                LogError("Failure: producing device certificate.");
                result = __FAILURE__;
            }
            else if (produce_root_ca(riot_info, tbs_sig) != 0)
            {
                LogError("Failure: producing root ca.");
                result = __FAILURE__;
            }
            else if (produce_priv_key(riot_info) != 0)
            {
                LogError("Failure: producing root ca private key.");
                result = __FAILURE__;
            }
            else if (mallocAndStrcpy_s(&riot_info->certificate_common_name, X509_ALIAS_TBS_DATA.SubjectCommon) != 0)
            {
                LogError("Failure: attempting to get common name");
                result = __FAILURE__;
            }
            else
            {
                result = 0;
            }
        }
    }
    return result;
}

int initialize_riot_system(void)
{
    // Only initialize one time
    if (g_digest_initialized == 0)
    {
        // Derive DeviceID key pair from CDI
        DiceSHA256(g_uds_seed, DICE_UDS_LENGTH, g_digest);

        // Derive CDI based on UDS and RIoT Core "measurement"
        DiceSHA256_2(g_digest, DICE_DIGEST_LENGTH, RAMDOM_DIGEST, DICE_DIGEST_LENGTH, g_CDI);
        g_digest_initialized = 1;
    }
    return 0;
}

void deinitialize_riot_system(void)
{
}

DPS_SECURE_DEVICE_HANDLE dps_hsm_riot_create(void)
{
    DPS_SECURE_DEVICE_INFO* result;
    /* Codes_SRS_SECURE_DEVICE_RIOT_07_001: [ On success dps_hsm_riot_create shall allocate a new instance of the device auth interface. ] */
    result = malloc(sizeof(DPS_SECURE_DEVICE_INFO) );
    if (result == NULL)
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_006: [ If any failure is encountered dps_hsm_riot_create shall return NULL ] */
        LogError("Failure: malloc DPS_SECURE_DEVICE_INFO.");
    }
    else
    {
        memset(result, 0, sizeof(DPS_SECURE_DEVICE_INFO));
        if (process_riot_key_info(result) != 0)
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_006: [ If any failure is encountered dps_hsm_riot_create shall return NULL ] */
            free(result);
            result = NULL;
        }
    }
    return result;
}

void dps_hsm_riot_destroy(DPS_SECURE_DEVICE_HANDLE handle)
{
    /* Codes_SRS_SECURE_DEVICE_RIOT_07_007: [ if handle is NULL, dps_hsm_riot_destroy shall do nothing. ] */
    if (handle != NULL)
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_008: [ dps_hsm_riot_destroy shall free the DPS_SECURE_DEVICE_HANDLE instance. ] */
        free(handle->certificate_common_name);
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_009: [ dps_hsm_riot_destroy shall free all resources allocated in this module. ] */
        free(handle);
    }
}

char* dps_hsm_riot_get_certificate(DPS_SECURE_DEVICE_HANDLE handle)
{
    char* result;
    if (handle == NULL)
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_010: [ if handle is NULL, dps_hsm_riot_get_certificate shall return NULL. ] */
        LogError("Invalid handle value specified");
        result = NULL;
    }
    else
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_011: [ dps_hsm_riot_get_certificate shall allocate a char* to return the riot certificate. ] */
        result = malloc(handle->alias_cert_length+1);
        if (result == NULL)
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_013: [ If any failure is encountered dps_hsm_riot_get_certificate shall return NULL ] */
            LogError("Failed to allocate cert buffer.");
        }
        else
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_012: [ On success dps_hsm_riot_get_certificate shall return the riot certificate. ] */
            memset(result, 0, handle->alias_cert_length+1);
            memcpy(result, handle->alias_cert_pem, handle->alias_cert_length);
        }
    }
    return result;
}

char* dps_hsm_riot_get_alias_key(DPS_SECURE_DEVICE_HANDLE handle)
{
    char* result;
    if (handle == NULL)
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_014: [ if handle is NULL, dps_hsm_riot_get_alias_key shall return NULL. ] */
        LogError("Invalid handle value specified");
        result = NULL;
    }
    else
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_015: [ dps_hsm_riot_get_alias_key shall allocate a char* to return the alias certificate. ] */
        if ((result = malloc(handle->alias_key_length+1)) == NULL)
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_017: [ If any failure is encountered dps_hsm_riot_get_alias_key shall return NULL ] */
            LogError("Failure allocating registration id.");
        }
        else
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_016: [ On success dps_hsm_riot_get_alias_key shall return the alias certificate. ] */
            memset(result, 0, handle->alias_key_length+1);
            memcpy(result, handle->alias_priv_key_pem, handle->alias_key_length);
        }
    }
    return result;
}

char* dps_hsm_riot_get_device_cert(DPS_SECURE_DEVICE_HANDLE handle)
{
    char* result;
    if (handle == NULL)
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_018: [ if handle is NULL, dps_hsm_riot_get_device_cert shall return NULL. ]*/
        LogError("Invalid handle value specified");
        result = NULL;
    }
    else
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_019: [ dps_hsm_riot_get_device_cert shall allocate a char* to return the device certificate. ] */
        if ((result = malloc(handle->device_id_length+1)) == NULL)
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_021: [ If any failure is encountered dps_hsm_riot_get_device_cert shall return NULL ] */
            LogError("Failure allocating registration id.");
        }
        else
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_020: [ On success dps_hsm_riot_get_device_cert shall return the device certificate. ] */
            memset(result, 0, handle->device_id_length+1);
            memcpy(result, handle->device_id_public_pem, handle->device_id_length);
        }
    }
    return result;
}

char* dps_hsm_riot_get_signer_cert(DPS_SECURE_DEVICE_HANDLE handle)
{
    char* result;
    if (handle == NULL)
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_022: [ if handle is NULL, dps_hsm_riot_get_signer_cert shall return NULL. ] */
        LogError("Invalid handle value specified");
        result = NULL;
    }
    else
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_023: [ dps_hsm_riot_get_signer_cert shall allocate a char* to return the signer certificate. ] */
        if ((result = malloc(handle->device_signed_length + 1)) == NULL)
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_025: [ If any failure is encountered dps_hsm_riot_get_signer_cert shall return NULL ] */
            LogError("Failure allocating registration id.");
        }
        else
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_024: [ On success dps_hsm_riot_get_signer_cert shall return the signer certificate. ] */
            memset(result, 0, handle->device_signed_length + 1);
            memcpy(result, handle->device_signed_pem, handle->device_signed_length);
        }
    }
    return result;
}

char* dps_hsm_riot_get_root_cert(DPS_SECURE_DEVICE_HANDLE handle)
{
    char* result;
    if (handle == NULL)
    {
        LogError("Invalid handle value specified");
        result = NULL;
    }
    else
    {
        if ((result = malloc(handle->root_ca_length + 1)) == NULL)
        {
            LogError("Failure allocating registration id.");
        }
        else
        {
            memset(result, 0, handle->root_ca_length + 1);
            memcpy(result, handle->root_ca_pem, handle->root_ca_length);
        }
    }
    return result;
}

char* dps_hsm_riot_get_root_key(DPS_SECURE_DEVICE_HANDLE handle)
{
    char* result;
    if (handle == NULL)
    {
        LogError("Invalid handle value specified");
        result = NULL;
    }
    else
    {
        if ((result = malloc(handle->root_ca_priv_length + 1)) == NULL)
        {
            LogError("Failure allocating registration id.");
        }
        else
        {
            memset(result, 0, handle->root_ca_priv_length + 1);
            memcpy(result, handle->root_ca_priv_pem, handle->root_ca_priv_length);
        }
    }
    return result;
}

char* dps_hsm_riot_get_common_name(DPS_SECURE_DEVICE_HANDLE handle)
{
    char* result;
    if (handle == NULL)
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_026: [ if handle is NULL, dps_hsm_riot_get_common_name shall return NULL. ] */
        LogError("Invalid handle value specified");
        result = NULL;
    }
    else
    {
        /* Codes_SRS_SECURE_DEVICE_RIOT_07_027: [ dps_hsm_riot_get_common_name shall allocate a char* to return the certificate common name. ] */
        if (mallocAndStrcpy_s(&result, handle->certificate_common_name) != 0)
        {
            /* Codes_SRS_SECURE_DEVICE_RIOT_07_028: [ If any failure is encountered dps_hsm_riot_get_signer_cert shall return NULL ] */
            LogError("Failure allocating common name.");
            result = NULL;
        }
    }
    return result;
}

const SEC_X509_INTERFACE* dps_hsm_x509_interface()
{
    /* Codes_SRS_SECURE_DEVICE_RIOT_07_029: [ dps_hsm_riot_interface shall return the SEC_RIOT_INTERFACE structure. ]*/
    return &sec_riot_interface;
}

char* dps_hsm_riot_create_leaf_cert(DPS_SECURE_DEVICE_HANDLE handle, const char* common_name)
{
    char* result;

    // The static data fields that make up the DeviceID Cert "to be signed" region
    RIOT_X509_TBS_DATA LEAF_CERT_TBS_DATA = {
        { 0x0E, 0x0D, 0x0C, 0x0B, 0x0A }, "", "MSR_TEST", "US",
        "170101000000Z", "370101000000Z", RIOT_SIGNER_NAME, "MSR_TEST", "US" };

    if (handle == NULL || common_name == NULL)
    {
        LogError("invalid parameter specified.");
        result = NULL;
    }
    else
    {
        RIOT_STATUS status;
        uint8_t der_buffer[DER_MAX_TBS] = { 0 };
        DERBuilderContext der_ctx = { 0 };
        RIOT_ECC_PUBLIC     leaf_id_pub;
        RIOT_ECC_PRIVATE    leaf_id_priv;
        RIOT_ECC_SIGNATURE tbs_sig = { 0 };

        LEAF_CERT_TBS_DATA.SubjectCommon = common_name;

        DERInitContext(&der_ctx, der_buffer, DER_MAX_TBS);
        if (X509GetDERCsrTbs(&der_ctx, &LEAF_CERT_TBS_DATA, &leaf_id_pub) != 0)
        {
            LogError("Failure: X509GetDeviceCertTBS");
            result = NULL;
        }
        // Sign the Alias Key Certificate's TBS region
        else if ((status = RiotCrypt_Sign(&tbs_sig, der_ctx.Buffer, der_ctx.Position, &leaf_id_priv)) != RIOT_SUCCESS)
        {
            LogError("Failure: RiotCrypt_Sign returned invalid status %d.", status);
            result = NULL;
        }
        // Create CSR for DeviceID
        else if (X509GetDERCsr(&der_ctx, &tbs_sig) != 0)
        {
            LogError("Failure: X509GetDERCsr");
            result = NULL;
        }
        else if ((result = malloc(DER_MAX_PEM + 1)) == NULL)
        {
            LogError("Failure allocating leaf cert");
        }
        else
        {
            memset(result, 0, DER_MAX_PEM+1);
            uint32_t leaf_len;
            if (DERtoPEM(&der_ctx, CERT_TYPE, result, &leaf_len) != 0)
            {
                LogError("Failure: DERtoPEM return invalid value.");
                free(result);
                result = NULL;
            }
        }
    }

    return result;
}
