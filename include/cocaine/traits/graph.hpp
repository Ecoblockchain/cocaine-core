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

#ifndef COCAINE_IO_DISPATCH_GRAPH_SERIALIZATION_TRAITS_HPP
#define COCAINE_IO_DISPATCH_GRAPH_SERIALIZATION_TRAITS_HPP

#include "cocaine/traits.hpp"
#include "cocaine/traits/map.hpp"
#include "cocaine/traits/optional.hpp"
#include "cocaine/traits/tuple.hpp"

#include "cocaine/rpc/graph.hpp"

namespace cocaine { namespace io {

template<>
struct type_traits<graph_node_t> {
    template<class Stream>
    static inline
    void
    pack(msgpack::packer<Stream>& target, const graph_node_t& source) {
        type_traits<graph_node_t::base_type>::pack(target, source);
    }

    static inline
    void
    unpack(const msgpack::object& source, graph_node_t& target) {
        type_traits<graph_node_t::base_type>::unpack(source, target);
    }
};

}} // namespace cocaine::io

#endif
