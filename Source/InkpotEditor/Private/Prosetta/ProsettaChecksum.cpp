#include "Prosetta/ProsettaChecksum.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

FString ProsettaChecksum::SHA256(const void* Data, uint32 ByteSize)
{
 uint8 digest[SHA256_DIGEST_LENGTH];
 ::SHA256(static_cast<const unsigned char*>(Data), ByteSize, digest);
 return BytesToHex(digest, SHA256_DIGEST_LENGTH).ToLower();
}
