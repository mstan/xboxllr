#include "machine/nv2a_generic_vertex_scratch.hpp"
#include "machine/nv2a_renderer.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__  \
                      << ": " #cond "\n";                                     \
            ++g_failures;                                                      \
        }                                                                      \
    } while (false)

using xlle::GenericVectorScratchLease;
using xlle::GenericVertexScratchStats;
using Vertex = xlle::Nv2aGenericCombinerVertex;

Vertex make_vertex(float value) {
    Vertex v{};
    v.position[0] = value;
    v.diffuse[0] = value + 1.0f;
    return v;
}

void test_early_return_restores_packet_storage() {
    bool enabled = true;
    uint64_t epoch = 7;
    GenericVertexScratchStats stats{};
    std::vector<Vertex> scratch;
    scratch.reserve(12);
    std::vector<Vertex> target;

    auto run = [&]() {
        GenericVectorScratchLease<Vertex> lease(scratch, target, enabled, epoch,
                                                stats, false);
        CHECK(target.empty());
        CHECK(target.capacity() >= 12);
        target.push_back(make_vertex(1.0f));
        target.push_back(make_vertex(2.0f));
        return;
    };
    run();

    CHECK(target.empty());
    CHECK(scratch.empty());
    CHECK(scratch.capacity() >= 12);
    CHECK(stats.packet_leases == 1);
    CHECK(stats.expansion_leases == 0);
    CHECK(stats.reuse_hits == 1);
    CHECK(stats.oversized_releases == 0);
}

void test_packet_and_expansion_swap_preserve_both_capacities() {
    bool enabled = true;
    uint64_t epoch = 3;
    GenericVertexScratchStats stats{};
    std::vector<Vertex> packet_scratch;
    std::vector<Vertex> expansion_scratch;
    packet_scratch.reserve(4);
    expansion_scratch.reserve(8);
    std::vector<Vertex> packet;

    {
        GenericVectorScratchLease<Vertex> packet_lease(
            packet_scratch, packet, enabled, epoch, stats, false);
        packet.push_back(make_vertex(1.0f));
        packet.push_back(make_vertex(2.0f));
        packet.push_back(make_vertex(3.0f));
        std::vector<Vertex> expanded;
        {
            GenericVectorScratchLease<Vertex> expanded_lease(
                expansion_scratch, expanded, enabled, epoch, stats, true);
            expanded.reserve(16);
            for (int i = 0; i < 6; ++i) expanded.push_back(make_vertex(float(i)));
            packet.swap(expanded);
            CHECK(packet.size() == 6);
            CHECK(expanded.size() == 3);
        }
        CHECK(expansion_scratch.empty());
        CHECK(expansion_scratch.capacity() >= 4);
        CHECK(packet.size() == 6);
    }

    CHECK(packet.empty());
    CHECK(packet_scratch.empty());
    CHECK(packet_scratch.capacity() >= 16);
    CHECK(stats.packet_leases == 1);
    CHECK(stats.expansion_leases == 1);
    CHECK(stats.reuse_hits == 2);
    CHECK(stats.oversized_releases == 0);
}

void test_empty_expansion_restores_storage() {
    bool enabled = true;
    uint64_t epoch = 0;
    GenericVertexScratchStats stats{};
    std::vector<Vertex> scratch;
    std::vector<Vertex> expanded;

    {
        GenericVectorScratchLease<Vertex> lease(scratch, expanded, enabled,
                                                epoch, stats, true);
        expanded.reserve(32);
    }

    CHECK(expanded.empty());
    CHECK(scratch.empty());
    CHECK(scratch.capacity() >= 32);
    CHECK(stats.expansion_leases == 1);
    CHECK(stats.reuse_hits == 0);
}

void test_capacity_limit_retains_exact_max_and_discards_larger() {
    bool enabled = true;
    uint64_t epoch = 0;
    GenericVertexScratchStats stats{};

    {
        std::vector<Vertex> scratch;
        std::vector<Vertex> target;
        {
            GenericVectorScratchLease<Vertex> lease(scratch, target, enabled,
                                                    epoch, stats, false);
            target.reserve(GenericVectorScratchLease<Vertex>::kMaxRetainedElements);
            target.push_back(make_vertex(4.0f));
        }
        CHECK(scratch.empty());
        CHECK(scratch.capacity() ==
              GenericVectorScratchLease<Vertex>::kMaxRetainedElements);
        CHECK(stats.oversized_releases == 0);
    }

    {
        std::vector<Vertex> scratch;
        std::vector<Vertex> target;
        {
            GenericVectorScratchLease<Vertex> lease(scratch, target, enabled,
                                                    epoch, stats, false);
            target.reserve(GenericVectorScratchLease<Vertex>::kMaxRetainedElements + 1);
            target.push_back(make_vertex(5.0f));
        }
        CHECK(scratch.empty());
        CHECK(scratch.capacity() == 0);
        CHECK(stats.oversized_releases == 1);
    }
}

void test_disable_while_lease_active_discards_on_return() {
    bool enabled = true;
    uint64_t epoch = 0;
    GenericVertexScratchStats stats{};
    std::vector<Vertex> scratch;
    scratch.reserve(10);
    std::vector<Vertex> target;

    {
        GenericVectorScratchLease<Vertex> lease(scratch, target, enabled, epoch,
                                                stats, false);
        CHECK(target.capacity() >= 10);
        enabled = false;
        ++epoch;
        xlle::generic_vector_scratch_release(scratch);
    }

    CHECK(scratch.empty());
    CHECK(scratch.capacity() == 0);
    // After a mid-lease disable, ownership stays with the still-live local
    // target until that target is destroyed. Production uses a fresh local target
    // inside the packet-construction scope, so this does not escape the call.
    CHECK(target.empty());
    CHECK(target.capacity() >= 10);
    CHECK(stats.packet_leases == 1);
    CHECK(stats.reuse_hits == 1);
}

void test_nested_lease_does_not_alias_and_epoch_blocks_old_restore() {
    bool enabled = true;
    uint64_t epoch = 2;
    GenericVertexScratchStats stats{};
    std::vector<Vertex> outer_scratch;
    std::vector<Vertex> nested_scratch;
    outer_scratch.reserve(11);
    nested_scratch.reserve(7);
    std::vector<Vertex> outer_target;
    std::vector<Vertex> nested_target;

    {
        GenericVectorScratchLease<Vertex> outer(
            outer_scratch, outer_target, enabled, epoch, stats, false);
        outer_target.push_back(make_vertex(1.0f));
        CHECK(outer_scratch.capacity() == 0);
        {
            GenericVectorScratchLease<Vertex> nested(
                nested_scratch, nested_target, enabled, epoch, stats, true);
            nested_target.push_back(make_vertex(2.0f));
            CHECK(outer_target.size() == 1);
            CHECK(nested_target.size() == 1);
            CHECK(&outer_target[0] != &nested_target[0]);
            enabled = false;
            ++epoch;
            xlle::generic_vector_scratch_release(outer_scratch);
            xlle::generic_vector_scratch_release(nested_scratch);
            enabled = true;
            ++epoch;
        }
        CHECK(nested_scratch.capacity() == 0);
        CHECK(outer_target.size() == 1);
    }

    CHECK(outer_scratch.capacity() == 0);
    CHECK(nested_scratch.capacity() == 0);
    CHECK(stats.packet_leases == 1);
    CHECK(stats.expansion_leases == 1);
    CHECK(stats.reuse_hits == 2);
}

void test_retained_byte_helper_and_explicit_release() {
    std::vector<Vertex> scratch;
    scratch.reserve(5);
    CHECK(xlle::generic_vector_scratch_retained_bytes(scratch) ==
          scratch.capacity() * sizeof(Vertex));
    xlle::generic_vector_scratch_release(scratch);
    CHECK(scratch.empty());
    CHECK(scratch.capacity() == 0);
}

void test_reentrant_borrow_of_same_member_preserves_outer_data() {
    bool enabled = true;
    uint64_t epoch = 0;
    GenericVertexScratchStats stats{};
    std::vector<Vertex> scratch;
    scratch.reserve(11);
    std::vector<Vertex> outer_target;
    const Vertex* original_storage = nullptr;
    {
        GenericVectorScratchLease<Vertex> outer(
            scratch, outer_target, enabled, epoch, stats, false);
        outer_target.push_back(make_vertex(42.0f));
        const auto* outer_data = outer_target.data();
        original_storage = outer_data;
        {
            std::vector<Vertex> inner_target;
            GenericVectorScratchLease<Vertex> inner(
                scratch, inner_target, enabled, epoch, stats, false);
            inner_target.reserve(7);
            inner_target.push_back(make_vertex(99.0f));
            CHECK(inner_target.data() != outer_data);
            CHECK(outer_target[0].position[0] == 42.0f);
        }
        CHECK(scratch.empty());
        CHECK(scratch.capacity() >= 7);
        CHECK(scratch.data() != outer_data);
        CHECK(outer_target.data() == outer_data);
        CHECK(outer_target[0].position[0] == 42.0f);
    }
    CHECK(scratch.empty());
    CHECK(scratch.data() == original_storage);
    CHECK(scratch.capacity() >= 11);
    CHECK(outer_target.empty());
    CHECK(stats.packet_leases == 2);
    CHECK(stats.reuse_hits == 1); // Inner borrow saw the member empty.
}

}  // namespace

int main() {
    test_early_return_restores_packet_storage();
    test_packet_and_expansion_swap_preserve_both_capacities();
    test_empty_expansion_restores_storage();
    test_capacity_limit_retains_exact_max_and_discards_larger();
    test_disable_while_lease_active_discards_on_return();
    test_nested_lease_does_not_alias_and_epoch_blocks_old_restore();
    test_retained_byte_helper_and_explicit_release();
    test_reentrant_borrow_of_same_member_preserves_outer_data();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
