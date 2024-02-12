#ifndef RecoTracker_PixelVertexFinding_plugins_alpaka_clusterTracksDBSCAN_h
#define RecoTracker_PixelVertexFinding_plugins_alpaka_clusterTracksDBSCAN_h

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <alpaka/alpaka.hpp>

#include "DataFormats/VertexSoA/interface/ZVertexSoA.h"
#include "HeterogeneousCore/AlpakaInterface/interface/HistoContainer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/PixelVertexFinding/interface/PixelVertexWorkSpaceLayout.h"

#include "vertexFinder.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::vertexFinder {

  using VtxSoAView = ::reco::ZVertexSoAView;
  using WsSoAView = ::vertexFinder::PixelVertexWorkSpaceSoAView;
  // this algo does not really scale as it works in a single block...
  // enough for <10K tracks we have
  class ClusterTracksDBSCAN {
  public:
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(const TAcc& acc,
                                  VtxSoAView pdata,
                                  WsSoAView pws,
                                  int minT,      // min number of neighbours to be "core"
                                  float eps,     // max absolute distance to cluster
                                  float errmax,  // max error to be "seed"
                                  float chi2max  // max normalized distance to cluster
    ) const {
      constexpr bool verbose = false;  // in principle the compiler should optmize out if false
      const uint32_t threadIdxLocal(alpaka::getIdx<alpaka::Block, alpaka::Threads>(acc)[0u]);
      if constexpr (verbose) {
        if (cms::alpakatools::once_per_block(acc))
          printf("params %d %f %f %f\n", minT, eps, errmax, chi2max);
      }
      auto er2mx = errmax * errmax;

      auto& __restrict__ data = pdata;
      auto& __restrict__ ws = pws;
      auto nt = ws.ntrks();
      float const* __restrict__ zt = ws.zt();
      float const* __restrict__ ezt2 = ws.ezt2();

      uint32_t& nvFinal = data.nvFinal();
      uint32_t& nvIntermediate = ws.nvIntermediate();

      uint8_t* __restrict__ izt = ws.izt();
      int32_t* __restrict__ nn = data.ndof();
      int32_t* __restrict__ iv = ws.iv();

      ALPAKA_ASSERT_OFFLOAD(zt);
      ALPAKA_ASSERT_OFFLOAD(iv);
      ALPAKA_ASSERT_OFFLOAD(nn);
      ALPAKA_ASSERT_OFFLOAD(ezt2);

      using Hist = cms::alpakatools::HistoContainer<uint8_t, 256, 16000, 8, uint16_t>;
      auto& hist = alpaka::declareSharedVar<Hist, __COUNTER__>(acc);
      auto& hws = alpaka::declareSharedVar<Hist::Counter[32], __COUNTER__>(acc);

      for (auto j : cms::alpakatools::uniform_elements(acc, Hist::totbins())) {
        hist.off[j] = 0;
      }
      alpaka::syncBlockThreads(acc);

      if constexpr (verbose) {
        if (cms::alpakatools::once_per_block(acc))
          printf("booked hist with %d bins, size %d for %d tracks\n", hist.nbins(), hist.capacity(), nt);
      }

      ALPAKA_ASSERT_OFFLOAD(static_cast<int>(nt) <= hist.capacity());

      // fill hist  (bin shall be wider than "eps")
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        ALPAKA_ASSERT_OFFLOAD(i < ::zVertex::MAXTRACKS);
        int iz = int(zt[i] * 10.);  // valid if eps<=0.1
        iz = std::clamp(iz, INT8_MIN, INT8_MAX);
        izt[i] = iz - INT8_MIN;
        ALPAKA_ASSERT_OFFLOAD(iz - INT8_MIN >= 0);
        ALPAKA_ASSERT_OFFLOAD(iz - INT8_MIN < 256);
        hist.count(acc, izt[i]);
        iv[i] = i;
        nn[i] = 0;
      }
      alpaka::syncBlockThreads(acc);
      if (threadIdxLocal < 32)
        hws[threadIdxLocal] = 0;  // used by prefix scan...
      alpaka::syncBlockThreads(acc);
      hist.finalize(acc, hws);
      alpaka::syncBlockThreads(acc);
      ALPAKA_ASSERT_OFFLOAD(hist.size() == nt);
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        hist.fill(acc, izt[i], uint32_t(i));
      }
      alpaka::syncBlockThreads(acc);

      // count neighbours
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        if (ezt2[i] > er2mx)
          continue;
        auto loop = [&](uint32_t j) {
          if (i == j)
            return;
          auto dist = std::abs(zt[i] - zt[j]);
          if (dist > eps)
            return;
          //        if (dist*dist>chi2max*(ezt2[i]+ezt2[j])) return;
          nn[i]++;
        };

        cms::alpakatools::forEachInBins(hist, izt[i], 1, loop);
      }

      alpaka::syncBlockThreads(acc);

      // find NN with smaller z...
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        if (nn[i] < minT)
          continue;  // DBSCAN core rule
        float mz = zt[i];
        auto loop = [&](uint32_t j) {
          if (zt[j] >= mz)
            return;
          if (nn[j] < minT)
            return;  // DBSCAN core rule
          auto dist = std::abs(zt[i] - zt[j]);
          if (dist > eps)
            return;
          //        if (dist*dist>chi2max*(ezt2[i]+ezt2[j])) return;
          mz = zt[j];
          iv[i] = j;  // assign to cluster (better be unique??)
        };
        cms::alpakatools::forEachInBins(hist, izt[i], 1, loop);
      }

      alpaka::syncBlockThreads(acc);

#ifdef GPU_DEBUG
      //  mini verification
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        if (iv[i] != int(i))
          ALPAKA_ASSERT_OFFLOAD(iv[iv[i]] != int(i));
      }
      alpaka::syncBlockThreads(acc);
#endif

      // consolidate graph (percolate index of seed)
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        auto m = iv[i];
        while (m != iv[m])
          m = iv[m];
        iv[i] = m;
      }

      alpaka::syncBlockThreads(acc);

#ifdef GPU_DEBUG
      //  mini verification
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        if (iv[i] != int(i))
          ALPAKA_ASSERT_OFFLOAD(iv[iv[i]] != int(i));
      }
      alpaka::syncBlockThreads(acc);
#endif

#ifdef GPU_DEBUG
      // and verify that we did not spit any cluster...
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        if (nn[i] < minT)
          continue;  // DBSCAN core rule
        ALPAKA_ASSERT_OFFLOAD(zt[iv[i]] <= zt[i]);
        auto loop = [&](uint32_t j) {
          if (nn[j] < minT)
            return;  // DBSCAN core rule
          auto dist = std::abs(zt[i] - zt[j]);
          if (dist > eps)
            return;
          //  if (dist*dist>chi2max*(ezt2[i]+ezt2[j])) return;
          // they should belong to the same cluster, isn't it?
          if (iv[i] != iv[j]) {
            printf("ERROR %d %d %f %f %d\n", i, iv[i], zt[i], zt[iv[i]], iv[iv[i]]);
            printf("      %d %d %f %f %d\n", j, iv[j], zt[j], zt[iv[j]], iv[iv[j]]);
            ;
          }
          ALPAKA_ASSERT_OFFLOAD(iv[i] == iv[j]);
        };
        cms::alpakatools::forEachInBins(hist, izt[i], 1, loop);
      }
      alpaka::syncBlockThreads(acc);
#endif

      // collect edges (assign to closest cluster of closest point??? here to closest point)
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        //    if (nn[i]==0 || nn[i]>=minT) continue;    // DBSCAN edge rule
        if (nn[i] >= minT)
          continue;  // DBSCAN edge rule
        float mdist = eps;
        auto loop = [&](uint32_t j) {
          if (nn[j] < minT)
            return;  // DBSCAN core rule
          auto dist = std::abs(zt[i] - zt[j]);
          if (dist > mdist)
            return;
          if (dist * dist > chi2max * (ezt2[i] + ezt2[j]))
            return;  // needed?
          mdist = dist;
          iv[i] = iv[j];  // assign to cluster (better be unique??)
        };
        cms::alpakatools::forEachInBins(hist, izt[i], 1, loop);
      }

      auto& foundClusters = alpaka::declareSharedVar<unsigned int, __COUNTER__>(acc);
      foundClusters = 0;
      alpaka::syncBlockThreads(acc);

      // find the number of different clusters, identified by a tracks with clus[i] == i;
      // mark these tracks with a negative id.
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        if (iv[i] == int(i)) {
          if (nn[i] >= minT) {
            auto old = alpaka::atomicInc(acc, &foundClusters, 0xffffffff, alpaka::hierarchy::Threads{});
            iv[i] = -(old + 1);
          } else {  // noise
            iv[i] = -9998;
          }
        }
      }
      alpaka::syncBlockThreads(acc);

      ALPAKA_ASSERT_OFFLOAD(foundClusters < ::zVertex::MAXVTX);

      // propagate the negative id to all the tracks in the cluster.
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        if (iv[i] >= 0) {
          // mark each track in a cluster with the same id as the first one
          iv[i] = iv[iv[i]];
        }
      }
      alpaka::syncBlockThreads(acc);

      // adjust the cluster id to be a positive value starting from 0
      for (auto i : cms::alpakatools::uniform_elements(acc, nt)) {
        iv[i] = -iv[i] - 1;
      }

      nvIntermediate = nvFinal = foundClusters;

      if constexpr (verbose) {
        if (cms::alpakatools::once_per_block(acc))
          printf("found %d proto vertices\n", foundClusters);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::vertexFinder

#endif  // RecoTracker_PixelVertexFinding_plugins_alpaka_clusterTracksDBSCAN_h
