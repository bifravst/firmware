/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
// #define CLOUD_CLIENT_ID "testingtesting"

#define CLOUD_CLIENT_PRIVATE_KEY                                               \
	"-----BEGIN RSA PRIVATE KEY-----\n"                                    \
	"MIIEpQIBAAKCAQEAzh00E6LPVsI538/IGaQj1by7kSvH1Hong8lgM9fMe1gegA2P\n"   \
	"iC65aARsNGq/yz9DUDWFO2yf1Jz4eyNgjQFBs4Jghb+mrGXns8XISahLWOLYmMqe\n"   \
	"xMwnm2fqR9a9kBiNfjQXDFiw3jidzSauUIWSXmuk3BJHBRgtQ0nPB1p7B/VeUhfn\n"   \
	"tV2yys5wIfZEv7NHv/EsCZQMsnC6U/nojyQ4EUtb2g5jHka8R+DyHN83thIypL6f\n"   \
	"6TUwq7MbCfThDK+1FVPJweIpVROUezCprrZDbN6BbVHLpbrel8IOS1Ns9qpE+wiz\n"   \
	"PJbtOhTpIjegCyqgUf+5gXNZOqysyo2Ks7VgpQIDAQABAoIBAQCa2O3K4sIKWBjK\n"   \
	"wHuAaARUC1qPEekrBCqzo5KW4EVLVUR0x7tRgGjicJAqQRieRYT4uXzCzDS3ssYr\n"   \
	"HMToqk0F46lIUleRpW2RbcGvNLSGrsYy4+arywTmiAdGuVno68lBSzkVmXwnNzm2\n"   \
	"ap0C+ZahQRW2EDUy6pr2tnjG+X781k0hbsnrHPGw2KCZ5+a1Ms3pShcen5CkXySF\n"   \
	"BjHe1GLayOlW0gHj9aX8OkOTbi2e/Lb5RWV8D8EFWI8KsLzfTgem1bvWy5msPsro\n"   \
	"14Xw/uxqiSZGbb2EsuQQ0xmSBV0dWZJimeFmk0bvLaCDYi0KRrBNE6lvCx2ciFYc\n"   \
	"WeYLVGOBAoGBAOw6tnHdjw2BL1IHgXfiOigUG/gZNGdq1F39hkaOzNFg1BtlILiN\n"   \
	"oq9+LeMqXFilujaw0rHS7xfywYud/0ByfSCXOvQlM0VBN9PhhxKwWQEFNs41KfCq\n"   \
	"TpLuKh7001kvcDzK2BLMeoG0yWc8pAvkKe8J6tCtGHTSux8KXTzCjn1pAoGBAN9d\n"   \
	"Qv60D80LLK49WjJaRddLX2bucj4ffjMC+d4UsNKnq2l4rOX0CS3zWyr+peOsxcP5\n"   \
	"w4wUiolWNVFC0pXZgwKyEEB35REVtP1aPMLlIc/dJ4o1U70v33hBIWqpt8HnbbHw\n"   \
	"XDK02By508awzGq4hriKjTYu5+CEx6yMduvabZXdAoGBAM7hoHRaF/U4xOtu/Viz\n"   \
	"fEErU09VK4rCiVgDNvxGBWP9C+UuDJj0GZzdwWwn5hYQleNdujfXxmLPy+btKOUV\n"   \
	"HzZSm2PDzIIDWtQpt/SLEneNTHENKDzHueZ9w8+2k/2QSRhEgTT9dPBxFs1d00FC\n"   \
	"weLBaa71WOy+vPezSPJ0ZPu5AoGBANnJ2JH6xdFazPUTk0e3Z5PVxS7a3n+eO3HH\n"   \
	"vBSDPioYHHWZmZQZz32DZGhWpS+KfcZpWPbT7ISejxwtuKEt1aUiM+B4RtzSuoex\n"   \
	"nb82pTJFY9FJz92OuSlK8CdNVoP1gKrYPz2dwX643jpElvyT8aAsUCX9tE/hh8PB\n"   \
	"rg6oCuPtAoGAHmXoZZS7FZ+suFOFavuyYh5f+sm2xA6eEPbVokdymeml9fdgFikI\n"   \
	"8BGfSdPOgS+QmhL7zndDRVmtc4BiMd+9TJCn2TBchfL85t0TFf1V6hKRAEO8vI5W\n"   \
	"A37lQk7OQaf3GwJjPfDLDCYYsvCi48Lz/0/xAhnCIReo3BzgtVC0xrg=\n"           \
	"-----END RSA PRIVATE KEY-----\n"

#define CLOUD_CLIENT_PUBLIC_CERTIFICATE                                        \
	"-----BEGIN CERTIFICATE-----\n"                                        \
	"MIIDBzCCAe+gAwIBAgIUcAZ8k71FpYCTyt3rjZvzLrwyGmwwDQYJKoZIhvcNAQEL\n"   \
	"BQAwEzERMA8GA1UECwwIYmlmcmF2c3QwHhcNMTkwNzI2MTQxMjM2WhcNMjAwNzI1\n"   \
	"MTQxMjM2WjATMREwDwYDVQQLDAhiaWZyYXZzdDCCASIwDQYJKoZIhvcNAQEBBQAD\n"   \
	"ggEPADCCAQoCggEBAL3KIABIIDDJzBmyeNORuN+8y6F7zUSiBvyFi1BwrRMesqDP\n"   \
	"2gEA0t49HvRHPSehGZml9+QD9Xw9J1R3CRowyE8ZTkhPw0hDAB+dNomouGJQ8GQd\n"   \
	"qjQrcahzortJjevwmuB5uD/cc3sZ+ew3GDpSD5YRL56fHsm/pi04/m0wjd0CkLbH\n"   \
	"r2VMVmvNL8/OQEU6BZGESs1DyO36tBmB0DASPWDVNNE+l4HGLLHnwcr4zVGapuVZ\n"   \
	"s/vswSWWk5b30jVIApksb3q8mPH/WjvX04mYT2ikAZDTy2AyIPFbiSUzrrBZcWJR\n"   \
	"RN+zx2CQZCWtIr6xelIKetXupb9ZKVI9s3Pl340CAwEAAaNTMFEwHQYDVR0OBBYE\n"   \
	"FLXH7BeIjXMC1a1mQhQUwjYbprQvMB8GA1UdIwQYMBaAFLXH7BeIjXMC1a1mQhQU\n"   \
	"wjYbprQvMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAGh1pbhz\n"   \
	"7uel7fTLlCzQ2Fg5Mzh1YVyDN/swsHp0igJ+s+ymwTOmMLdnktukiE7I4qXZj/LU\n"   \
	"Xdsl7ZuaIgfpWa/aYDY4LuncDXJu4sF75twzVGNNrDOt0cmbYnpwuVjRINUlNKUe\n"   \
	"t9AyM4LLZj0e50qrm8V+qAXxw87cx2Ho5P92jP1jqEiqTqNihHi75N4uWfgulAHc\n"   \
	"2CRxzj+nKVnudB15PAvx0B9LosIJ1/3ixO+T8ytEdfsmeNDX8ZbNK6920mJHhLfB\n"   \
	"3eUoqB9bGvFlTY3fz4zx7Yj16CCpWEa+8Ytt3yUW1LlKo+HTZDrgSnLGCki1+VDV\n"   \
	"6PIt2ts8O2Ru1sU=\n"                                                   \
	"-----END CERTIFICATE-----\n"

#define CLOUD_CA_CERTIFICATE                                                   \
	"-----BEGIN CERTIFICATE-----\n"                                        \
	"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"   \
	"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"   \
	"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"   \
	"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"   \
	"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"   \
	"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"   \
	"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"   \
	"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"   \
	"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"   \
	"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"   \
	"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"   \
	"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"   \
	"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"   \
	"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"   \
	"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"   \
	"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"   \
	"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"   \
	"rqXRfboQnoZsG4q5WTP468SQvvG5\n"                                       \
	"-----END CERTIFICATE-----\n"
