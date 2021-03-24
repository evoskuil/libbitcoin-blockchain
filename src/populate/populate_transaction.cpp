/**
 * Copyright (c) 2011-2017 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/blockchain/populate/populate_transaction.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <bitcoin/system.hpp>
#include <bitcoin/blockchain/define.hpp>
#include <bitcoin/blockchain/interface/fast_chain.hpp>
#include <bitcoin/blockchain/pools/branch.hpp>

namespace libbitcoin {
namespace blockchain {

using namespace bc::chain;
using namespace std::placeholders;

#define NAME "populate_transaction"

// Database access is limited to calling populate_base.

populate_transaction::populate_transaction(dispatcher& dispatch,
    const fast_chain& chain)
  : populate_base(dispatch, chain)
{
}

void populate_transaction::populate(transaction_const_ptr tx,
    result_handler&& handler) const
{
    const auto state = tx->validation.state;
    BITCOIN_ASSERT(state);

    // Chain state is for the next block, so always > 0.
    BITCOIN_ASSERT(tx->validation.state->height() > 0);
    const auto chain_height = tx->validation.state->height() - 1u;

    //*************************************************************************
    // CONSENSUS:
    // It is OK for us to restrict *pool* transactions to those that do not
    // collide with any in the chain (as well as any in the pool) as collision
    // will result in monetary destruction and we don't want to facilitate it.
    // We must allow collisions in *block* validation if that is configured as
    // otherwise will will not follow the chain when a collision is mined.
    //*************************************************************************
    populate_base::populate_duplicate(chain_height, *tx, false);

    // Because txs include no proof of work we much short circuit here.
    // Otherwise a peer can flood us with repeat transactions to validate.
    if (tx->validation.duplicate)
    {
        handler(error::unspent_duplicate);
        return;
    }

    const auto total_inputs = tx->inputs().size();

    // Return if there are no inputs to validate (will fail later).
    if (total_inputs == 0)
    {
        handler(error::success);
        return;
    }

    const auto buckets = std::min(dispatch_.size(), total_inputs);
    const auto join_handler = synchronize(std::move(handler), buckets, NAME);
    BITCOIN_ASSERT(buckets != 0);

    for (size_t bucket = 0; bucket < buckets; ++bucket)
        dispatch_.concurrent(&populate_transaction::populate_inputs,
            this, tx, chain_height, bucket, buckets, join_handler);
}

void populate_transaction::populate_inputs(transaction_const_ptr tx,
    size_t chain_height, size_t bucket, size_t buckets,
    result_handler handler) const
{
    BITCOIN_ASSERT(bucket < buckets);
    const auto& inputs = tx->inputs();

    for (auto input_index = bucket; input_index < inputs.size();
        input_index = ceiling_add(input_index, buckets))
    {
        const auto& input = inputs[input_index];
        const auto& prevout = input.previous_output();
        populate_prevout(chain_height, prevout, false);
    }

    handler(error::success);
}

} // namespace blockchain
} // namespace libbitcoin
