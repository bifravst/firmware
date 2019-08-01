/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#define CLOUD_CLIENT_ID "testingtesting"

#define CLOUD_CLIENT_PRIVATE_KEY                                               \
	"-----BEGIN RSA PRIVATE KEY-----\n"                                    \
	"MIIEpQIBAAKCAQEAzu2GiT+VuhRH2Ietq8ctt1ssLK5H6vP0u2Dz/0a9y7pxyTPP\n"   \
	"4nd2teDKRLo2wYZgiJuR2A3HB1XbvqbBDOsTmlhL3ya6urYEu1AH/G89LaObEs70\n"   \
	"vULkk/mk13FW5LWc6iNQSbFziUev1O0sHWmdacAN5nrKDc1g7ikbw3zp8hjTVA8S\n"   \
	"V7LfijW9BIof3mSFeeR9fq+aG6aTJSoBy/JG4txN7f4E/1NM8nxtMr1VIiHOqaLY\n"   \
	"3v4LV+JK+02dL6FzCiAwqAwma3LYVJ2gTxoK4RW2M/da/tzh8NWtPVBOa335Lz0D\n"   \
	"HPlD35/w86P3mH0bKRzguDHkguqWSx8h973oMQIDAQABAoIBAQCrE0jVE5KP4tB7\n"   \
	"PcDhcaxkGKZu0i93GfXNLJzNAglL83q7I3DNBINKXuwa4fD/Ej+g8S0keE+BywP/\n"   \
	"nRGhwn/UbQddGEHstys2STYxBy6HGunMJPnFtxYPGKelznhOYa+3CzcHlgO1DWVb\n"   \
	"HjIIpxaTJUrYr894lcF7ZNUlS5KGqW77J+9dPD3xvwms+uJOc8S48AMNIYVyA/DF\n"   \
	"blIEUE2KlhuUub3eUeOswOt0pgMynsev2KjWCq28LTnurbyFLXbX1kxyqejzXNWq\n"   \
	"+2DvR06L7MddIXT0ouEX0Snw+86tvpwH2G0l1Off8Sy75GRv/LM2W+YNz5gGArK5\n"   \
	"Gef2k4TZAoGBAOp5pwSno/dGZgyZ4xaywBtQN2Vj5yamZ4ouQis1gYY4xX77Md0U\n"   \
	"sbOCSgouAXSOEDqKcg0ZSPhzWlfKRZy8FxWAJRefiZjUbVqEP6AOGHy1L1HJA5Pc\n"   \
	"q0NhljUG8CNRH7iLpxoDfIYKOu6+vpOubPy4o3Rsv9PwAX/ReICxA7EPAoGBAOHs\n"   \
	"fR0+6QI5HBK/2QSvXqlSIGYMmxl4qRM4ZVdm7Rna9G4J25gEaTajqomyqCqCY3VN\n"   \
	"unlSKNU1/eZXKX67XgI7AT+GyKIldO+A2mCgrErKLzu/OsqcT+z1ulz6VXkVX2gK\n"   \
	"tQ+wzdIeMIgX6wyPY2vTq9qQWDFDE5uQlQoIWlK/AoGBALCWUBfsXUtkhISm/OTi\n"   \
	"WFX5ss60T6jHGCF0Nzctg8/fP7YjXmlfJXnI/RPvk/8A4u4DyGNfEJq03WxSlNy2\n"   \
	"tzflG4pQB6PHEFhkUzqqgvygw/N3TS91uLH1c9eZ0w72EMq+ummYCJc2ay3VD9hP\n"   \
	"PBuUvt127X2jOq3Vx0g8iEg3AoGBAIvWqnaQuv8iREsirnxk5C3f5Kflw5bXhaec\n"   \
	"77VSww2O5l66AU8t48XrNiK3D7oILPGto+92OEoIeli5uLh11zGAPjyI++TJVIDu\n"   \
	"e7z1ls9QKD5OFmDUsfAVBT6JwKAK55vpjLrij/MvtpB2ZYnHsx1JzoShdcVAJIHU\n"   \
	"0zt7ghTXAoGAGISseE7+VAICb7UbhmFFAfiZaYIorCySWlNY7W8TbJyUQ18gzxML\n"   \
	"+UqQxm4IvEnfhpnt97rBkii5rG8wcXtpQaZrBIWlf1ZIkKRRw/Co+QEL6S/2tt0Z\n"   \
	"JC1VHahVGyMaCoSF7krlTOmLlQJCjhMNV85ZKchLI7tujC5OdG4uJFA=\n"           \
	"-----END RSA PRIVATE KEY-----\n"

#define CLOUD_CLIENT_PUBLIC_CERTIFICATE                                        \
	"-----BEGIN CERTIFICATE-----\n"                                        \
	"MIIDWjCCAkKgAwIBAgIVAOpXfBZGRELLbmv/0oXoE6hdhTwiMA0GCSqGSIb3DQEB\n"   \
	"CwUAME0xSzBJBgNVBAsMQkFtYXpvbiBXZWIgU2VydmljZXMgTz1BbWF6b24uY29t\n"   \
	"IEluYy4gTD1TZWF0dGxlIFNUPVdhc2hpbmd0b24gQz1VUzAeFw0xOTA3MTExMDE2\n"   \
	"MDdaFw00OTEyMzEyMzU5NTlaMB4xHDAaBgNVBAMME0FXUyBJb1QgQ2VydGlmaWNh\n"   \
	"dGUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDO7YaJP5W6FEfYh62r\n"   \
	"xy23Wywsrkfq8/S7YPP/Rr3LunHJM8/id3a14MpEujbBhmCIm5HYDccHVdu+psEM\n"   \
	"6xOaWEvfJrq6tgS7UAf8bz0to5sSzvS9QuST+aTXcVbktZzqI1BJsXOJR6/U7Swd\n"   \
	"aZ1pwA3mesoNzWDuKRvDfOnyGNNUDxJXst+KNb0Eih/eZIV55H1+r5obppMlKgHL\n"   \
	"8kbi3E3t/gT/U0zyfG0yvVUiIc6potje/gtX4kr7TZ0voXMKIDCoDCZrcthUnaBP\n"   \
	"GgrhFbYz91r+3OHw1a09UE5rffkvPQMc+UPfn/Dzo/eYfRspHOC4MeSC6pZLHyH3\n"   \
	"vegxAgMBAAGjYDBeMB8GA1UdIwQYMBaAFA7+PCGms3NKrLs48kFOTsVT9IwKMB0G\n"   \
	"A1UdDgQWBBSsVCUWRE6GpM6mxufyLQbXFULQ6TAMBgNVHRMBAf8EAjAAMA4GA1Ud\n"   \
	"DwEB/wQEAwIHgDANBgkqhkiG9w0BAQsFAAOCAQEAnVnk+Tx+TFFGMKlNOFVdkewz\n"   \
	"1SdlxStqkJK9FedV7qrPUo/lXcbDLy+8dfuC28rZ+B91YfZka7rgNSNqX4k9jmMR\n"   \
	"Xo6ooc0fViZFFqEV8pX1n+Vsdtc3EVo95/K2zCogb8FBcrEts/F3tTdC+U9/5Z2c\n"   \
	"Etvero3wsSDkMBkiggI7dtJYZs1tuTyAPBh0/vGKKl0BeCgdU2Bj+ZxLKgU1Pkll\n"   \
	"F2zUKawu8CxDH1qwE+R1ig1ljhhR0HjhAtfKi6f7bkuSovw+i3wPqf3OsMHqNU0G\n"   \
	"OGmjfLV0OD2dkthD73xD1GUN6VDd1t/brODxfLCz/9QTKkU8BNqciNLRAIA5yw==\n"   \
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
