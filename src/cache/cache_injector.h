#pragma once

#include <boost/asio/spawn.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/system/error_code.hpp>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <queue>

#include "../namespaces.h"
#include "../util/crypto.h"
#include "cache_entry.h"
#include "db.h"

namespace asio_ipfs { class node; }
namespace ouinet { namespace bittorrent { class MainlineDht; }}

namespace ouinet {

class Bep44InjectorDb;
class BTreeInjectorDb;
class Publisher;
class Scheduler;

class CacheInjector {
public:
    using OnInsert = std::function<void(boost::system::error_code, std::string)>;
    using Request  = http::request<http::string_body>;
    using Response = http::response<http::dynamic_body>;

    // Assorted data resulting from an insertion.
    struct InsertionResult {
        std::string key;  // key to look up descriptor
        std::string desc_data;  // serialized descriptor
        std::string desc_link;  // descriptor storage link
        std::string db_ins_data;  // db-specific data to help reinsert
    };

public:
    CacheInjector( boost::asio::io_service&
                 , util::Ed25519PrivateKey bt_privkey
                 , fs::path path_to_repo);

    CacheInjector(const CacheInjector&) = delete;
    CacheInjector& operator=(const CacheInjector&) = delete;

    // Returns the IPNS CID of the database.
    // The database could be then looked up by e.g. pointing your browser to:
    // "https://ipfs.io/ipns/" + ipfs.id()
    std::string ipfs_id() const;

    // Insert a descriptor with the given `id` for the given request and response
    // into the db given by `DbType`, along with data in distributed storage.
    InsertionResult insert_content( const std::string& id
                                  , const Request&
                                  , const Response&
                                  , DbType
                                  , boost::asio::yield_context);

    // Find the content previously stored by the injector under `url`.
    // The descriptor identifier and cached content are returned.
    //
    // Basically it does this: Look into the database to find the IPFS_ID
    // correspoinding to the `url`, when found, fetch the content corresponding
    // to that IPFS_ID from IPFS.
    std::pair<std::string, CacheEntry> get_content( std::string url
                                                  , DbType
                                                  , Cancel&
                                                  , boost::asio::yield_context);

    std::string get_descriptor( std::string url
                              , DbType
                              , Cancel&
                              , boost::asio::yield_context);

    ~CacheInjector();

private:
    InjectorDb* get_db(DbType) const;

private:
    std::unique_ptr<asio_ipfs::node> _ipfs_node;
    std::unique_ptr<bittorrent::MainlineDht> _bt_dht;
    std::unique_ptr<Publisher> _publisher;
    std::unique_ptr<BTreeInjectorDb> _btree_db;
    std::unique_ptr<Bep44InjectorDb> _bep44_db;
    const unsigned int _concurrency = 8;
    std::unique_ptr<Scheduler> _scheduler;
    std::shared_ptr<bool> _was_destroyed;
};

} // namespace

