/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "Parser.h"
#include <stdio.h>

namespace mdns {
namespace Minimal {

bool QueryData::Parse(const BytesRange & validData, const uint8_t ** start)
{
    // Structure is:
    //    QNAME
    //    TYPE
    //    CLASS (plus a flag for unicast)

    if (!validData.Contains(*start))
    {
        return false;
    }

    const uint8_t * nameEnd = nullptr;
    {
        SerializedQNameIterator it(validData, *start);
        nameEnd = it.FindDataEnd();
    }
    if (nameEnd == nullptr)
    {
        return false;
    }

    if (!validData.Contains(nameEnd + 4))
    {
        return false;
    }

    // TODO: should there be checks for valid mType/class?

    mType = static_cast<QType>(chip::Encoding::BigEndian::Read16(nameEnd));

    uint16_t klass = chip::Encoding::BigEndian::Read16(nameEnd);

    mAnswerViaUnicast = (klass & Query::kUnicastAnswerFlag) != 0;
    mClass            = static_cast<QClass>(klass & ~Query::kUnicastAnswerFlag);
    mNameIterator     = SerializedQNameIterator(validData, *start);

    *start = nameEnd;

    return true;
}

bool ResourceData::Parse(const BytesRange & validData, const uint8_t ** start)
{
    // Structure is:
    //    QNAME
    //    TYPE      (16 bit)
    //    CLASS     (16 bit)
    //    TTL       (32 bit)
    //    RDLENGTH  (16 bit)
    //    <DATA>    (RDLENGTH bytpes)
    if (!validData.Contains(*start))
    {
        return false;
    }

    const uint8_t * nameEnd = nullptr;

    {
        SerializedQNameIterator it(validData, *start);
        nameEnd = it.FindDataEnd();
    }
    if (nameEnd == nullptr)
    {
        return false;
    }

    // need 3*u16 + u32
    if (!validData.Contains(nameEnd + 10))
    {
        return false;
    }

    mType  = static_cast<QType>(chip::Encoding::BigEndian::Read16(nameEnd));
    mClass = static_cast<QClass>(chip::Encoding::BigEndian::Read16(nameEnd));
    mTtl   = chip::Encoding::BigEndian::Read32(nameEnd);

    uint16_t dataLen = chip::Encoding::BigEndian::Read16(nameEnd); // resource data

    if (!validData.Contains(nameEnd + dataLen - 1))
    {
        return false; // no space for RDATA
    }
    mData = BytesRange(nameEnd, nameEnd + dataLen);

    mNameIterator = SerializedQNameIterator(validData, *start);

    *start = nameEnd + dataLen;

    return true;
}

bool ParsePacket(const BytesRange & packetData, ParserDelegate * delegate)
{
    if (packetData.Size() < static_cast<ptrdiff_t>(HeaderRef::kSizeBytes))
    {
        return false;
    }

    // header is used as const, so cast is safe
    HeaderRef header(const_cast<uint8_t *>(packetData.Start()));

    if (!header.GetFlags().IsValidMdns())
    {
        return false;
    }

    delegate->OnHeader(header);

    const uint8_t * data = packetData.Start() + HeaderRef::kSizeBytes;

    {
        QueryData queryData;
        for (unsigned i = 0; i < header.GetQueryCount(); i++)
        {
            if (!queryData.Parse(packetData, &data))
            {
                return false;
            }

            delegate->OnQuery(queryData);
        }
    }

    {
        ResourceData resourceData;
        for (unsigned i = 0; i < header.GetAnswerCount(); i++)
        {
            if (!resourceData.Parse(packetData, &data))
            {
                return false;
            }

            delegate->OnResource(ParserDelegate::ResourceType::kAnswer, resourceData);
        }

        for (unsigned i = 0; i < header.GetAuthorityCount(); i++)
        {
            if (!resourceData.Parse(packetData, &data))
            {
                return false;
            }

            delegate->OnResource(ParserDelegate::ResourceType::kAuthority, resourceData);
        }

        for (unsigned i = 0; i < header.GetAdditionalCount(); i++)
        {
            if (!resourceData.Parse(packetData, &data))
            {
                return false;
            }

            delegate->OnResource(ParserDelegate::ResourceType::kAdditional, resourceData);
        }
    }

    return true;
}

} // namespace Minimal
} // namespace mdns