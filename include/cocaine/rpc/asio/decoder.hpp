/*
    Copyright (c) 2011-2014 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2014 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef COCAINE_IO_DECODER_HPP
#define COCAINE_IO_DECODER_HPP

#include "cocaine/errors.hpp"

#include "cocaine/hpack/header.hpp"
#include "cocaine/hpack/msgpack_traits.hpp"

#include "cocaine/rpc/protocol.hpp"

#include "cocaine/traits.hpp"
#include "cocaine/traits/tuple.hpp"

#include <boost/range/algorithm/find_if.hpp>

namespace cocaine { namespace io {

struct decoder_t;

namespace aux {

struct decoded_message_t {
    friend struct io::decoder_t;

    auto
    span() const -> uint64_t {
        return object.via.array.ptr[0].as<uint64_t>();
    }

    auto
    type() const -> uint64_t {
        return object.via.array.ptr[1].as<uint64_t>();
    }

    template<class Event, class Traits = event_traits<Event>>
    auto
    args() const -> typename tuple::fold<typename Traits::sequence_type>::type {
        typename tuple::fold<typename Traits::sequence_type>::type args;

        try {
            // NOTE: Unpacks the object into a tuple using the argument typelist unlike using plain
            // sequence typelist, in order to support parameter tags, like optional<T>.
            type_traits<typename Traits::argument_type>::unpack(object.via.array.ptr[2], args);
        } catch(const msgpack::type_error& e) {
            throw std::system_error(error::invalid_argument, e.what());
        }

        return args;
    }

    template<class Header>
    auto
    meta() const -> boost::optional<hpack::header_t> {
        auto it = boost::find_if(metadata, [](const hpack::header_t& element) -> bool {
            return element.get_name() == Header::name();
        });

        return it == metadata.end() ? boost::none : boost::make_optional(*it);
    }

private:
    // These objects keep references to message buffer in the Decoder.
    msgpack::object object;
    std::vector<hpack::header_t> metadata;
};

} // namespace aux

struct decoder_t {
    COCAINE_DECLARE_NONCOPYABLE(decoder_t)

    decoder_t() = default;
   ~decoder_t() = default;

    typedef aux::decoded_message_t message_type;

    size_t
    decode(const char* data, size_t size, message_type& message, std::error_code& ec) {
        size_t offset = 0;

        msgpack::unpack_return rv = msgpack::unpack(data, size, &offset, &zone, &message.object);

        if(rv == msgpack::UNPACK_SUCCESS || rv == msgpack::UNPACK_EXTRA_BYTES) {
            if(message.object.type != msgpack::type::ARRAY || message.object.via.array.size < 3) {
                ec = error::frame_format_error;
            } else if(message.object.via.array.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
                      message.object.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
                      message.object.via.array.ptr[2].type != msgpack::type::ARRAY)
            {
                ec = error::frame_format_error;
            } else if(message.object.via.array.size > 3) {
                if(message.object.via.array.ptr[3].type != msgpack::type::ARRAY) {
                    ec = error::frame_format_error;
                } else if(!hpack::msgpack_traits::unpack_vector(
                          message.object.via.array.ptr[3], hpack_context, message.metadata))
                {
                    ec = error::hpack_error;
                }
            }
        } else if(rv == msgpack::UNPACK_CONTINUE) {
            ec = error::insufficient_bytes;
        } else if(rv == msgpack::UNPACK_PARSE_ERROR) {
            ec = error::parse_error;
        }

        return offset;
    }

private:
    msgpack::zone zone;

    // HPACK HTTP/2.0 tables.
    hpack::header_table_t hpack_context;
};

}} // namespace cocaine::io

#endif
