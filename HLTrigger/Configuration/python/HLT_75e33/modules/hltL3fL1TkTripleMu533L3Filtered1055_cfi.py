import FWCore.ParameterSet.Config as cms

hltL3fL1TkTripleMu533L3Filtered1055 = cms.EDFilter("HLTMuonTrkL1TkMuFilter",
    inputCandCollection = cms.InputTag("hltPhase2L3MuonCandidates"),
    inputMuonCollection = cms.InputTag("hltPhase2L3Muons"),
    maxAbsEta = cms.double(2.5),
    maxNormalizedChi2 = cms.double(1e+99),
    minMuonHits = cms.int32(-1),
    minMuonStations = cms.int32(1),
    minN = cms.uint32(1),
    minPt = cms.double(10.0),
    minTrkHits = cms.int32(-1),
    algoBlockTag = cms.InputTag("l1tGTAlgoBlockProducer"),
    algoNames = cms.vstring("pTripleTkMuon5_3_3"),
    saveTags = cms.bool(True)
)
