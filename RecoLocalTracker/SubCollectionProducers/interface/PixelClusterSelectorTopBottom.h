#ifndef RecoSelectors_PixelClusterSelectorTopBottom_h
#define RecoSelectors_PixelClusterSelectorTopBottom_h

/* \class PixelClusterSelectorTopBottom
*
* \author Giuseppe Cerati, INFN
*
*
*/

#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "DataFormats/Common/interface/Handle.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/CommonDetUnit/interface/PixelGeomDetUnit.h"
#include "DataFormats/DetId/interface/DetId.h"
#include "FWCore/Utilities/interface/InputTag.h"

class PixelClusterSelectorTopBottom : public edm::global::EDProducer<> {
public:
  explicit PixelClusterSelectorTopBottom(const edm::ParameterSet& cfg)
      : tTrackerGeom_(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord>()),
        token_(consumes<SiPixelClusterCollectionNew>(cfg.getParameter<edm::InputTag>("label"))),
        y_(cfg.getParameter<double>("y")) {
    produces<SiPixelClusterCollectionNew>();
  }

  void produce(edm::StreamID, edm::Event& event, const edm::EventSetup& setup) const override;

private:
  edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> const tTrackerGeom_;
  edm::EDGetTokenT<SiPixelClusterCollectionNew> token_;
  double y_;
};

#endif
