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

#ifndef COCAINE_IO_DISPATCH_HPP
#define COCAINE_IO_DISPATCH_HPP

#include "cocaine/common.hpp"
#include "cocaine/locked_ptr.hpp"

#include "cocaine/rpc/slot/blocking.hpp"
#include "cocaine/rpc/slot/deferred.hpp"
#include "cocaine/rpc/slot/streamed.hpp"

#include "cocaine/rpc/traversal.hpp"

#include <boost/mpl/transform.hpp>
#include <boost/mpl/lambda.hpp>

#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>
#include <boost/variant/variant.hpp>

namespace cocaine {

template<class Tag> class dispatch;

namespace io {

class basic_dispatch_t {
    // The name of the service which this protocol implementation belongs to. Mostly used for logs,
    // and for synchronization stuff in the Locator Service.
    const std::string m_name;

public:
    explicit
    basic_dispatch_t(const std::string& name);

    virtual
   ~basic_dispatch_t();

    // Concrete protocol transition as opposed to transition description in protocol graphs. It can
    // either be some new dispatch pointer, an uninitialized pointer - terminal transition, or just
    // an empty optional - recurrent transition (i.e. no transition at all).

    virtual
    boost::optional<dispatch_ptr_t>
    process(const decoder_t::message_type& message, const upstream_ptr_t& upstream) const = 0;

    // Called on abnormal transport destruction. The idea's if the client disconnects unexpectedly,
    // i.e. not reaching the end of the dispatch graph, then some special handling might be needed.
    // Think 'zookeeper ephemeral nodes'.

    virtual
    void
    discard(const std::error_code& ec) const;

    // Observers

    virtual
    auto
    root() const -> const graph_root_t& = 0;

    auto
    name() const -> std::string;

    virtual
    int
    version() const = 0;
};

} // namespace io

namespace mpl = boost::mpl;

template<class Tag>
class dispatch:
    public io::basic_dispatch_t
{
    static const io::graph_root_t kProtocol;

    // Slot construction

    typedef typename mpl::transform<
        typename io::messages<Tag>::type,
        typename mpl::lambda<std::shared_ptr<io::basic_slot<mpl::_1>>>::type
    >::type slot_types;

    typedef std::map<
        int,
        typename boost::make_variant_over<slot_types>::type
    > slot_map_t;

    synchronized<slot_map_t> m_slots;

public:
    explicit
    dispatch(const std::string& name):
        basic_dispatch_t(name)
    { }

    template<class Event, class F>
    dispatch&
    on(const F& callable, typename boost::disable_if<io::is_slot<F, Event>>::type* = nullptr);

    template<class Event>
    dispatch&
    on(const std::shared_ptr<io::basic_slot<Event>>& ptr);

    template<class Event>
    void
    forget();

public:
    virtual
    boost::optional<io::dispatch_ptr_t>
    process(const io::decoder_t::message_type& message, const io::upstream_ptr_t& upstream) const;

    virtual
    auto
    root() const -> const io::graph_root_t& {
        return kProtocol;
    }

    virtual
    int
    version() const {
        return io::protocol<Tag>::version::value;
    }

    // Generic API

    template<class Visitor>
    typename Visitor::result_type
    process(int id, const Visitor& visitor) const;
};

template<class Tag>
const io::graph_root_t dispatch<Tag>::kProtocol = io::traverse<Tag>().get();

namespace aux {

// Slot selection

template<class R, class Event>
struct select {
    typedef io::blocking_slot<Event> type;
};

template<class R, class Event>
struct select<deferred<R>, Event> {
    typedef io::deferred_slot<deferred, Event> type;
};

template<class R, class Event>
struct select<streamed<R>, Event> {
    typedef io::deferred_slot<streamed, Event> type;
};

// Slot invocation with arguments

struct calling_visitor_t:
    public boost::static_visitor<boost::optional<io::dispatch_ptr_t>>
{
    calling_visitor_t(const io::decoder_t::message_type& unpacked_, const io::upstream_ptr_t& upstream_):
        unpacked(unpacked_),
        upstream(upstream_)
    { }

    template<class Event>
    result_type
    operator()(const std::shared_ptr<io::basic_slot<Event>>& slot) const {
        typedef io::basic_slot<Event> slot_type;

        // Call the slot with the upstream constrained with the event's upstream protocol type tag.
        return result_type((*slot)(unpacked.args<Event>(), typename slot_type::upstream_type(upstream)));
    }

private:
    const io::decoder_t::message_type& unpacked;
    const io::upstream_ptr_t& upstream;
};

} // namespace aux

template<class Tag>
template<class Event, class F>
dispatch<Tag>&
dispatch<Tag>::on(const F& callable, typename boost::disable_if<io::is_slot<F, Event>>::type*) {
    typedef typename aux::select<
        typename result_of<F>::type,
        Event
    >::type slot_type;

    return on<Event>(std::make_shared<slot_type>(callable));
}

template<class Tag>
template<class Event>
dispatch<Tag>&
dispatch<Tag>::on(const std::shared_ptr<io::basic_slot<Event>>& ptr) {
    typedef io::event_traits<Event> traits;

    if(!m_slots->insert(std::make_pair(traits::id, ptr)).second) {
        throw std::system_error(error::duplicate_slot, Event::alias());
    }

    return *this;
}

template<class Tag>
template<class Event>
void
dispatch<Tag>::forget() {
    if(!m_slots->erase(io::event_traits<Event>::id)) {
        throw std::system_error(error::slot_not_found);
    }
}

template<class Tag>
boost::optional<io::dispatch_ptr_t>
dispatch<Tag>::process(const io::decoder_t::message_type& message, const io::upstream_ptr_t& upstream) const {
    return process(message.type(), aux::calling_visitor_t(message, upstream));
}

template<class Tag>
template<class Visitor>
typename Visitor::result_type
dispatch<Tag>::process(int id, const Visitor& visitor) const {
    typedef typename slot_map_t::mapped_type slot_ptr_type;

    const auto slot = m_slots.apply([&](const slot_map_t& mapping) -> slot_ptr_type {
        typename slot_map_t::const_iterator lb, ub;

        // NOTE: Using equal_range() here, instead of find() to check for slot existence and get the
        // slot pointer in one call instead of two.
        std::tie(lb, ub) = mapping.equal_range(id);

        if(lb != ub) {
            // NOTE: The slot pointer is copied here, allowing the handling code to unregister slots
            // via dispatch<T>::forget() without pulling the object from underneath itself.
            return lb->second;
        } else {
            throw std::system_error(error::slot_not_found);
        }
    });

    return boost::apply_visitor(visitor, slot);
}

} // namespace cocaine

#endif
