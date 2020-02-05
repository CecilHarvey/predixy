/*
 * predixy - A high performance and full features proxy for redis.
 * Copyright (C) 2017 Joyield, Inc. <joyield.com@gmail.com>
 * All rights reserved.
 */

#include <algorithm>
#include "Auth.h"
#include "Conf.h"
#include "Request.h"


Auth::Auth(int mode):
    mMode(mode),
    mReadKeyPrefix(nullptr),
    mWriteKeyPrefix(nullptr)
{
}

Auth::Auth(const AuthConf& conf):
    mPassword(conf.password.c_str(), conf.password.size()),
    mMode(conf.mode),
    mReadKeyPrefix(nullptr),
    mWriteKeyPrefix(nullptr),
    mIPWhiteList(nullptr)
{
    if (!conf.readKeyPrefix.empty()) {
        mReadKeyPrefix = new KeyPrefixSet(conf.readKeyPrefix.begin(), conf.readKeyPrefix.end());
    }
    if (!conf.writeKeyPrefix.empty()) {
        mWriteKeyPrefix = new KeyPrefixSet(conf.writeKeyPrefix.begin(), conf.writeKeyPrefix.end());
    }
    if (!conf.IPWhiteList.empty()) {
        mIPWhiteList = new KeyPrefixSet(conf.IPWhiteList.begin(), conf.IPWhiteList.end());
    }
    if ((!mReadKeyPrefix || !mWriteKeyPrefix) && !conf.keyPrefix.empty()) {
        auto kp = new KeyPrefixSet(conf.keyPrefix.begin(), conf.keyPrefix.end());
        if (!mReadKeyPrefix) {
            mReadKeyPrefix = kp;
        }
        if (!mWriteKeyPrefix) {
            mWriteKeyPrefix = kp;
        }
    }
}

Auth::~Auth()
{
    if (mReadKeyPrefix) {
        delete mReadKeyPrefix;
    }
    if (mWriteKeyPrefix && mWriteKeyPrefix != mReadKeyPrefix) {
        delete mWriteKeyPrefix;
    }
}

bool Auth::IPAllowed(const String& peer) const
{
    if (!mIPWhiteList) {
        return true; // ip white list not set, allow all ip
    }
    char ip[64];
    const char *p = (const char *)peer;
    int i;
    for (i = 0; i < 63; i++) {
        if (p[i] == ':' || p[i] == '\0') {
            break;
        }
        ip[i] = p[i];
    }
    ip[i] = '\0';
    auto it = mIPWhiteList->find(ip);
    return it != mIPWhiteList->end();
}

bool Auth::permission(Request* req, const String& key) const
{
    auto& cmd = Command::get(req->type());
    if (!(mMode & cmd.mode)) {
        return false;
    }
    const KeyPrefixSet* kp = nullptr;
    if (cmd.mode & Command::Read) {
        kp = mReadKeyPrefix;
    } else if (cmd.mode & Command::Write) {
        kp = mWriteKeyPrefix;
    }
    if (kp) {
        auto it = kp->upper_bound(key);
        const String* p = nullptr;
        if (it == kp->end()) {
            p = &(*kp->rbegin());
        } else if (it != kp->begin()) {
            p = &(*--it);
        }
        return p && key.hasPrefix(*p);
    }
    return true;
}

Auth Authority::AuthAllowAll;
Auth Authority::AuthDenyAll(0);

Authority::Authority():
    mDefault(&AuthAllowAll)
{
    if (mDefault->count() == 0)
    {
        mDefault->ref();
    }
    pthread_rwlock_init(&mLock, nullptr);
}

Authority::~Authority()
{
    for (auto& i : mAuthMap) {
        i.second->unref();
    }
    mAuthMap.clear();
    pthread_rwlock_destroy(&mLock);
}

void Authority::add(const AuthConf& ac)
{
    Auth* a = Auth::Allocator::create(ac);
    auto it = mAuthMap.find(a->password());
    if (it != mAuthMap.end()) {
        Auth* p = it->second;
        mAuthMap.erase(it);
        p->unref();
    }
    a->ref();
    mAuthMap[a->password()] = a;
    if (a->password().empty()) {
        mDefault = a;
    } else if (mDefault == &AuthAllowAll) {
        mDefault = &AuthDenyAll;
        if (mDefault->count() == 0)
        {
            mDefault->ref();
        }
    }
}
