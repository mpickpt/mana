/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "lookup_service.h"
#include "../jalib/jassert.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

string
LookupService::getSummaryStats()
{
  ostringstream o;
  size_t totalKeys = 0;
  size_t totalSize = 0;
  for (ConstMapIterator i = _maps.begin(); i != _maps.end(); i++) {
    const KeyValueMap &kvmap = i->second;
    o << i->first  << ": " << kvmap.size();
    totalKeys += kvmap.size();
    KeyValueMap::const_iterator it;
    for (it = kvmap.begin(); it != kvmap.end(); it++) {
      const KeyValue &k = it->first;
      KeyValue *v = it->second;
      size_t kvmapSize = ((k.len() + v->len()) * kvmap.size());
      totalSize += kvmapSize;
      o << " (keyLen: " << k.len() << ", valLen: " << v->len() << ")";
      o << " totalSize: " << kvmapSize  << " (" << kvmapSize / 1024 << " KB)";
      break;
    }
  }
  ostringstream o2;
  o2 << "Nameservice database stats:"
     << "\n#databases:  " << _maps.size()
     << "\n#total keys: " << totalKeys
     << "\n#total size: " << totalSize << " (" << totalSize / 1024 << " KB)"
     << "\nIndividual database stats:\n"
     << o.str();
  return o2.str();
}

const KeyValueMap*
LookupService::getMap(string mapName) const
{
  ConstMapIterator map = _maps.find(mapName);
  if (map != _maps.end()) {
    return &_maps.at(mapName);
  }
  return NULL;
}

void
LookupService::reset()
{
  MapIterator i;

  for (i = _maps.begin(); i != _maps.end(); i++) {
    KeyValueMap &kvmap = i->second;
    KeyValueMap::iterator it;
    for (it = kvmap.begin(); it != kvmap.end(); it++) {
      KeyValue *k = (KeyValue *)&(it->first);
      KeyValue *v = it->second;
      k->destroy();
      v->destroy();
      delete v;
    }
    kvmap.clear();
  }
  _maps.clear();
  _lastUniqueIds.clear();
  _offsets.clear();
}

void
LookupService::addKeyValue(string id,
                           const void *key,
                           size_t keyLen,
                           const void *val,
                           size_t valLen)
{
  KeyValueMap &kvmap = _maps[id];

  KeyValue k(key, keyLen);
  KeyValue *v = new KeyValue(val, valLen);

  if (kvmap.find(k) != kvmap.end()) {
    JTRACE("Duplicate key");
  }
  kvmap[k] = v;
}

void
LookupService::query(string id,
                     const void *key,
                     size_t keyLen,
                     void **val,
                     size_t *valLen)
{
  KeyValueMap &kvmap = _maps[id];
  KeyValue k(key, keyLen);

  if (kvmap.find(k) == kvmap.end()) {
    JTRACE("Lookup Failed, Key not found.");
    *val = NULL;
    *valLen = 0;
    return;
  }

  KeyValue *v = kvmap[k];
  *valLen = v->len();
  *val = new char[v->len()];
  memcpy(*val, v->data(), *valLen);
}

void
LookupService::registerData(const DmtcpMessage &msg, const void *data)
{
  JASSERT(msg.keyLen > 0 && msg.valLen > 0 &&
          msg.keyLen + msg.valLen == msg.extraBytes)
    (msg.keyLen) (msg.valLen) (msg.extraBytes);
  const void *key = data;
  const void *val = (char *)key + msg.keyLen;
  size_t keyLen = msg.keyLen;
  size_t valLen = msg.valLen;
  addKeyValue(msg.nsid, key, keyLen, val, valLen);
}

void
LookupService::respondToQuery(jalib::JSocket &remote,
                              const DmtcpMessage &msg,
                              const void *key)
{
  JASSERT(msg.keyLen > 0 && msg.keyLen == msg.extraBytes)
    (msg.keyLen) (msg.extraBytes);
  void *val = NULL;
  size_t valLen = 0;
  DmtcpMessage reply;

  if (msg.type == DMT_NAME_SERVICE_GET_UNIQUE_ID) {
    reply.type = DMT_NAME_SERVICE_GET_UNIQUE_ID_RESPONSE;
    getUniqueId(msg.nsid, key, msg.keyLen, &val,
                msg.uniqueIdOffset, msg.valLen);
    valLen = msg.valLen;
  } else {
    reply.type = DMT_NAME_SERVICE_QUERY_RESPONSE;
    query(msg.nsid, key, msg.keyLen, &val, &valLen);
  }

  reply.keyLen = 0;
  reply.valLen = valLen;
  reply.extraBytes = reply.valLen;

  remote << reply;
  if (valLen > 0) {
    remote.writeAll((char *)val, valLen);
  }
  delete[] (char *)val;
}

void
LookupService::getUniqueId(const char *id,    // DB name
                           const void *key,   // Key: can be hostid, pid, etc.
                           size_t key_len,    // Length of the key
                           void **val,        // Result
                           uint32_t offset,   // Difference in two unique ids
                           size_t val_len)    // Expected value length
{
  KeyValueMap &kvmap = _maps[id];
  KeyValue k(key, key_len);

  // if key does not exist in the key-value map, add it
  if (kvmap.find(k) == kvmap.end()) {
    if (_lastUniqueIds.find(id) == _lastUniqueIds.end()) {
      _lastUniqueIds[id] = 1;
      _offsets[id] = offset;
    }
    JTRACE("Assigning a new unique id to client request")
       (id) (_lastUniqueIds[id]);
    KeyValue *v = new KeyValue(&_lastUniqueIds[id], val_len);
    _lastUniqueIds[id] += _offsets[id];
    kvmap[k] = v;
  }

  KeyValue *v = kvmap[k];
  JASSERT(v->len() == val_len);
  *val = new char[v->len()];
  memcpy(*val, v->data(), val_len);
}

void
LookupService::queryAll(const string& id, void **buf, size_t *buflen)
{
  ostringstream o;
  map<KeyValue, KeyValue *>::iterator i;
  KeyValueMap &kvmap = _maps[id];

  for (i = kvmap.begin(); i != kvmap.end(); i++) {
    KeyValue *k = (KeyValue *)&(i->first);
    KeyValue *v = i->second;
    size_t len;

    // insert the key length and key
    len = k->len();
    o.write((const char*)(&len), sizeof(len));
    o.write((const char*)k->data(), len);
    // insert the value length and value
    len = v->len();
    o.write((const char*)(&len), sizeof(len));
    o.write((const char*)v->data(), len);
  }

  *buflen = o.tellp();
  if (buflen == 0) {
    return;
  }

  *buf = new char[*buflen];
  if (!(*buf)) {
    return;
  }

  memcpy(*buf, o.str().c_str(), *buflen);
}

void
LookupService::sendAllMappings(jalib::JSocket &remote,
                               const DmtcpMessage &msg)
{
  void *val = NULL;
  size_t valLen = 0;

  DmtcpMessage reply(DMT_NAME_SERVICE_QUERY_ALL_RESPONSE);

  queryAll(msg.nsid, &val, &valLen);

  reply.keyLen = 0;
  reply.valLen = valLen;
  reply.extraBytes = reply.valLen;

  remote << reply;
  if (valLen > 0) {
    remote.writeAll((char *)val, valLen);
  }
  if (val) {
    delete[] (char *)val;
  }
}
