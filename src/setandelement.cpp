/**
* @file setandelement.cpp
* @brief Class for manipulating Sets and their Elements
*
* (c) 2013-2022 by Mega Limited, Wellsford, New Zealand
*
* This file is part of the MEGA SDK - Client Access Engine.
*
* Applications using the MEGA API must present a valid application key
* and comply with the the rules set forth in the Terms of Service.
*
* The MEGA SDK is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* @copyright Simplified (2-clause) BSD License.
*
* You should have received a copy of the license along with this
* program.
*/

#include "mega/base64.h"
#include "mega/setandelement.h"
#include "mega/utils.h"

using namespace ::std;


namespace mega {

    void CommonSE::setName(string&& name)
    {
        setAttr(nameTag, move(name));
    }

    void CommonSE::setAttr(const string& tag, string&& value)
    {
        if (!mAttrs)
        {
            mAttrs.reset(new string_map());
        }
        (*mAttrs)[tag] = move(value);
    }

    void CommonSE::rebaseCommonAttrsOn(const string_map* baseAttrs)
    {
        if (!baseAttrs)
        {
            return; // nothing to do
        }

        if (!mAttrs)
        {
            mAttrs.reset(new string_map());
        }

        // copy missing attributes
        if (mAttrs->empty()) // small optimizations
        {
            *mAttrs = *baseAttrs;
        }
        else
        {
            string_map rebased = *baseAttrs;
            for (auto& a : *mAttrs)
            {
                if (a.second.empty())
                {
                    rebased.erase(a.first);
                }
                else
                {
                    rebased[a.first].swap(a.second);
                }
            }
            mAttrs->swap(rebased);
        }

        if (mAttrs->empty())
        {
            mAttrs.reset();
        }
    }

    bool CommonSE::hasAttrChanged(const string& tag, const unique_ptr<string_map>& otherAttrs) const
    {
        string otherValue;
        if (otherAttrs)
        {
            auto it = otherAttrs->find(tag);
            otherValue = it != otherAttrs->end() ? it->second : "";
        }

        const string& value = getAttr(tag);
        return value != otherValue;
    }

    const string& CommonSE::getAttr(const string& tag) const
    {
        static const string value;
        if (!mAttrs)
        {
            return value;
        }

        auto it = mAttrs->find(tag);
        return it != mAttrs->end() ? it->second : value;
    }

    bool CommonSE::decryptAttributes(std::function<bool(const string&, const string&, string_map&)> f)
    {
        if (!mEncryptedAttrs) // 'at' was not received
        {
            return true;
        }

        if (mEncryptedAttrs->empty()) // 'at' was received empty
        {
            mAttrs.reset(new string_map());
            mEncryptedAttrs.reset();
            return true;
        }

        unique_ptr<string_map> newAttrs(new string_map());

        if (f(*mEncryptedAttrs, mKey, *newAttrs))
        {
            mAttrs.swap(newAttrs);
            mEncryptedAttrs.reset();
            return true;
        }

        return false;
    }

    string CommonSE::encryptAttributes(std::function<string(const string_map&, const string&)> f) const
    {
        if (!mAttrs || mAttrs->empty())
        {
            return string();
        }

        return f(*mAttrs, mKey);
    }


    handle Set::cover() const
    {
        string hs = getAttr(coverTag);
        if (!hs.empty())
        {
            handle h = 0;
            Base64::atob(hs.c_str(), (byte*)&h, SetElement::HANDLESIZE);
            return h;
        }

        return UNDEF;
    }

    void Set::setCover(handle h)
    {
        if (h == UNDEF)
        {
            setAttr(coverTag, string());
        }
        else
        {
            Base64Str<SetElement::HANDLESIZE> b64s(h);
            setAttr(coverTag, string(b64s.chars));
        }
    }

    bool Set::serialize(string* d)
    {
        CacheableWriter r(*d);

        r.serializehandle(mId);
        r.serializehandle(mUser);
        r.serializecompressed64(mTs);
        r.serializestring(mKey);

        size_t asize = mAttrs ? mAttrs->size() : 0;
        r.serializeu32((uint32_t)asize);
        if (asize)
        {
            for (auto& aa : *mAttrs)
            {
                r.serializestring(aa.first);
                r.serializestring(aa.second);
            }
        }

        r.serializeexpansionflags();

        return true;
    }

    unique_ptr<Set> Set::unserialize(string* d)
    {
        handle id = 0, u = 0;
        uint64_t ts = 0;
        string k;
        uint32_t attrCount = 0;

        CacheableReader r(*d);
        if (!r.unserializehandle(id) ||
            !r.unserializehandle(u) ||
            !r.unserializecompressed64(ts) ||
            !r.unserializestring(k) ||
            !r.unserializeu32(attrCount))
        {
            return nullptr;
        }

        // get all attrs
        string_map attrs;
        for (uint32_t i = 0; i < attrCount; ++i)
        {
            string ak, av;
            if (!r.unserializestring(ak) ||
                !r.unserializestring(av))
            {
                return nullptr;
            }
            attrs[move(ak)] = move(av);
        }

        unsigned char expansionsS[8];
        if (!r.unserializeexpansionflags(expansionsS, 0))
        {
            return nullptr;
        }

        auto s = ::mega::make_unique<Set>(id, move(k), u, move(attrs));
        s->setTs(ts);

        return s;
    }

    bool Set::updateWith(Set&& s)
    {
        setTs(s.ts());

        if (hasAttrChanged(nameTag, s.mAttrs)) setChanged(CH_NAME);
        if (hasAttrChanged(coverTag, s.mAttrs)) setChanged(CH_COVER);
        mAttrs.swap(s.mAttrs);

        return changes();
    }


    bool SetElement::updateWith(SetElement&& el)
    {
        if (el.hasOrder())
        {
            setOrder(el.order());
        }
        setTs(el.ts());
        // attrs of existing Element should be replaced if any of them has been updated, or
        // if they have been completely cleared (by the last 'aep' command)
        if (el.hasAttrs() || el.hasAttrsClearedByLastUpdate())
        {
            if (hasAttrChanged(nameTag, el.mAttrs)) setChanged(CH_EL_NAME);

            mAttrs.swap(el.mAttrs);
        }

        return changes();
    }

    void SetElement::setOrder(int64_t order)
    {
        if (!mOrder)
        {
            mOrder.reset(new int64_t(order));
            setChanged(CH_EL_ORDER);
        }
        else if (*mOrder != order)
        {
            *mOrder = order;
            setChanged(CH_EL_ORDER);
        }
    }

    bool SetElement::serialize(string* d)
    {
        CacheableWriter r(*d);

        r.serializehandle(mSetId);
        r.serializehandle(mId);
        r.serializenodehandle(mNodeHandle);
        r.serializei64(mOrder ? *mOrder : 0); // it will always have Order
        r.serializecompressed64(mTs);
        r.serializestring(mKey);

        size_t asize = mAttrs ? mAttrs->size() : 0;
        r.serializeu32((uint32_t)asize);
        if (asize)
        {
            for (auto& aa : *mAttrs)
            {
                r.serializestring(aa.first);
                r.serializestring(aa.second);
            }
        }

        r.serializeexpansionflags();

        return true;
    }

    unique_ptr<SetElement> SetElement::unserialize(string* d)
    {
        handle sid = 0, eid = 0;
        handle h = 0;
        int64_t o = 0;
        uint64_t ts = 0;
        string k;
        uint32_t attrCount = 0;
        unsigned char expansionsE[8];

        CacheableReader r(*d);
        if (!r.unserializehandle(sid) ||
            !r.unserializehandle(eid) ||
            !r.unserializenodehandle(h) ||
            !r.unserializei64(o) ||
            !r.unserializecompressed64(ts) ||
            !r.unserializestring(k) ||
            !r.unserializeu32(attrCount))
        {
            return nullptr;
        }

        // get all attrs
        string_map attrs;
        for (size_t i = 0; i < attrCount; ++i)
        {
            string ak, av;
            if (!r.unserializestring(ak) ||
                !r.unserializestring(av))
            {
                return nullptr;
            }
            attrs[move(ak)] = move(av);
        }

        if (!r.unserializeexpansionflags(expansionsE, 0))
        {
            return nullptr;
        }

        auto el = ::mega::make_unique<SetElement>(sid, h, eid, move(k), move(attrs));
        el->setOrder(o);
        el->setTs(ts);

        return el;
    }

} //namespace
