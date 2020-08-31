
#include "Pandora/AlgorithmHeaders.h"

#include "larpandoracontent/LArHelpers/LArPointingClusterHelper.h"
#include "larpandoracontent/LArObjects/LArPointingCluster.h"

#include "larpandoracontent/LArMonitoring/NeutrinoEventValidationAlgorithm.h"

#include "larpandoracontent/LArHelpers/LArGeometryHelper.h"
#include "larpandoracontent/LArObjects/LArTwoDSlidingFitResult.h"

#include "ParticleHierarchyValidationAlgorithm.h"

#include "larpandoracontent/LArHelpers/LArInteractionTypeHelper.h"
#include "larpandoracontent/LArHelpers/LArMonitoringHelper.h"
#include "larpandoracontent/LArHelpers/LArPfoHelper.h"
#include "larpandoracontent/LArHelpers/LArMCParticleHelper.h"

#include "larpandoracontent/LArMonitoring/EventValidationBaseAlgorithm.h"
#include "larpandoracontent/LArMonitoring/TestBeamEventValidationAlgorithm.h"

using namespace pandora;
using namespace std;

namespace development_area
{ 
    ParticleHierarchyValidationAlgorithm::ParticleHierarchyValidationAlgorithm():
        m_writeToTree(false),
        m_treeName("ElectronEventTree"),
        m_fileName("ElectronTree.root"),
        eventNo(0),
        addAll(false),   //In future, add this as an optional in the xml (ReadSettings), this just decides whether or not to add all particles to the relevance list or just the ones that meet criteria
        m_showAllPfoData(false),
        m_showAllMCPData(false)
    {
    }
    ParticleHierarchyValidationAlgorithm::~ParticleHierarchyValidationAlgorithm()
    {
        if (m_writeToTree)
        {
            PandoraMonitoringApi::SaveTree(this->GetPandora(), m_treeName.c_str(), m_fileName.c_str(), "UPDATE");
        }
        
    }

    StatusCode ParticleHierarchyValidationAlgorithm::Run(){
        
        //std::cout << "1" << std::endl;
        
        //SET THE EVE DISPLAY PARAMETERS
        PANDORA_MONITORING_API(SetEveDisplayParameters(this->GetPandora(), true, DETECTOR_VIEW_XZ, -1, 1, 1));
    
        m_primaryParameters.m_minPrimaryGoodHits = 15 /*15*/;
        m_primaryParameters.m_minHitsForGoodView = 5 /*5*/;
        m_primaryParameters.m_minPrimaryGoodViews = 2;
        m_primaryParameters.m_selectInputHits = false; /*true*/
        m_primaryParameters.m_maxPhotonPropagation = 2.5f;
        m_primaryParameters.m_minHitSharingFraction = 0.9f /*0.9f*/;
        m_primaryParameters.m_foldBackHierarchy = false;
    
            //For the root tree to know what event pfos and mcps came from
        eventNo+=1;
        std::cout << "<--Event Number " << eventNo << "-->" << std::endl;
        
        
                    //#---Defining and collecting all of the lists to do with the event---#
                    
            //(pointer to) List of PFOs (might need name)
        const PfoList *Pfos(nullptr);
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, "RecreatedPfos" /*m_inputPfoListName*/, Pfos));
        
            //(pointer to) List of MCParticles (might need name)
        const MCParticleList *MCParts(nullptr);
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, "Input" /*m_inputMCParticleListName*/, MCParts));


            //CaloHitLists
        const CaloHitList *CaloHitsU(nullptr);
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, "CaloHitListU", CaloHitsU));
        
        const CaloHitList *CaloHitsV(nullptr);
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, "CaloHitListV", CaloHitsV));
        
        const CaloHitList *CaloHitsW(nullptr);
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, "CaloHitListW", CaloHitsW));
        
        const CaloHitList *CaloHits(nullptr);
        PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraContentApi::GetList(*this, "CaloHitList2D", CaloHits));
        
        
        //std::cout << "2" << std::endl;
        
        bool MCElectronFound  = false;
        bool PfoElectronFound = false;
        
            //At this point, relevance is decided by "is it either electron flavour or muon flavoured (but not neutrinos)"
        PfoList relevantPfos;
        getRelevantPfos(Pfos, relevantPfos, addAll, PfoElectronFound);
        MCParticleList relevantMCParts;
        getRelevantMCParts(MCParts, relevantMCParts, addAll, MCElectronFound);
          
        /*                            
        if (MCElectronFound && !PfoElectronFound)
        {
            PANDORA_MONITORING_API(VisualizeParticleFlowObjects(this->GetPandora(), Pfos, "All Pfos", RED, true, true));
            PANDORA_MONITORING_API(VisualizeMCParticles(this->GetPandora(), MCParts, "All MCParticles", BLUE));
            PANDORA_MONITORING_API(ViewEvent(this->GetPandora()));
        }   
        */  
        
        
        
                    //########### NEW METHOD ###########
        for (const MCParticle *const c_MCPart : *MCParts)
        {  
            float c_PfoCompleteness(-1);
            float c_PfoPurity(-1);
            int c_PfoSharedHits(-1);
            float c_PfoEfficiency(-1);
               
                //Finding the hits per view 
            int NUHits(-1);
            int NVHits(-1);
            int NWHits(-1);
            getMCParticleViewHits(c_MCPart, NUHits, NVHits, NWHits, m_showAllMCPData, MCParts, CaloHits);
            
                //Get the PDG code of the best matched MCParticle
            int MCPId = abs(c_MCPart->GetParticleId());
            
                //Find the angle to the Muon (from the electron) if this MCParticle is an electron
                //Also find the Muon-Beam angle and the Michel-Beam angle
            float michelAngle(-1.0);
            float muonBeamAngle(-1.0);
            float michelBeamAngle(-1.0);
            CartesianVector* beamDirection = new CartesianVector(0,0,1);
            float PI = 2*acos(0.0);
            
            if (MCPId == 11)
            {
                CartesianVector* resultant = new CartesianVector(0,0,0);
                CartesianVector origin   = c_MCPart->GetVertex();
                CartesianVector endpoint = c_MCPart->GetEndpoint();
                //PANDORA_MONITORING_API(AddMarkerToVisualization(this->GetPandora(), &origin, "Electron Start", RED, 1));
                //PANDORA_MONITORING_API(AddMarkerToVisualization(this->GetPandora(), &endpoint, "Electron End", RED, 1));
                
                    //Resultant vector for the electron
                resultant = new CartesianVector(endpoint.GetX() - origin.GetX(), endpoint.GetY() - origin.GetY(), endpoint.GetZ() - origin.GetZ()); 
                
                        //Finding the parent Muon of the michel electron
                const MCParticleList parentList = c_MCPart->GetParentList();
                const MCParticle* parent(nullptr);
                bool found_parent = false;
                for (const MCParticle* const c_parent : parentList)
                {
                    if (abs(c_parent->GetParticleId()) == 13)
                    {
                        parent       = c_parent;
                        found_parent = true;
                        break;
                    }
                }
                        
                if (found_parent)
                { 
                    CartesianVector* parentResultant = new CartesianVector(0,0,0);
                    origin   = parent->GetVertex();
                    endpoint = parent->GetEndpoint();
                    //PANDORA_MONITORING_API(AddMarkerToVisualization(this->GetPandora(), &origin, "Muon Start", BLUE, 1));
                    //PANDORA_MONITORING_API(AddMarkerToVisualization(this->GetPandora(), &endpoint, "Muon End", BLUE, 1));
                            
                        //Resultant vector of the parent Muon
                    parentResultant = new CartesianVector(endpoint.GetX() - origin.GetX(), endpoint.GetY() - origin.GetY(), endpoint.GetZ() - origin.GetZ());
                            
                    //PANDORA_MONITORING_API(VisualizeParticleFlowObjects(this->GetPandora(), Pfos, "All Pfos", RED, true, true));
                    //PANDORA_MONITORING_API(VisualizeMCParticles(this->GetPandora(), MCParts, "All MCParticles", BLUE));
                    
                        //Calculate the Michel-Muon angle
                    michelAngle   = acos( resultant->GetCosOpeningAngle(*parentResultant) ) * 180/PI;
                    
                        //Calcualte the Muon-Beam angle       
                    muonBeamAngle = acos( parentResultant->GetCosOpeningAngle(*beamDirection) ) * 180/PI;
                }
                
                    //Calculate the Michel-Beam angle
                michelBeamAngle   = acos( resultant->GetCosOpeningAngle(*beamDirection) ) * 180/PI;
            }
            
            
                //Find the Pfo that this is the best match of (if any)
            const ParticleFlowObject* matchedPfo(nullptr);
            for (const ParticleFlowObject* c_Pfo : relevantPfos)
            {
                const MCParticle* bestMatch(nullptr);
                bestMatch = findBestMatch(c_Pfo, MCParts, CaloHits, m_primaryParameters);
                if (bestMatch == c_MCPart)
                { 
                    matchedPfo = c_Pfo;
                    break;
                }
            }
            
            if (matchedPfo != nullptr)
            {
                    //Finding the Completeness, Purity and number of shared hits
                std::vector<float> CandP = ParticleHierarchyValidationAlgorithm::purityAndCompleteness(matchedPfo, c_MCPart, MCParts, CaloHits, m_primaryParameters);
                c_PfoCompleteness = CandP[0];
                c_PfoPurity       = CandP[1];
                c_PfoSharedHits   = CandP[2];
            }
            
            int matched(-1);
                //Placing a cut on what particles are put into the table
            if (m_writeToTree && c_PfoSharedHits > 5 && c_PfoPurity > 0.5 && c_PfoCompleteness > 0.1 && matchedPfo != nullptr)
            {  
                matched = 1;
                c_PfoEfficiency = 1;    //Efficiency = 1 because we must have (at least) 1 'matched' Pfo to the MCParticle, as we are inside the if statement threshold condition
            }else
            {
                matched = 0;
                c_PfoEfficiency = 0;
            }
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "eventNo", eventNo);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "MCPPDG", MCPId);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "Matched", matched);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "UHits", NUHits);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "VHits", NVHits);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "WHits", NWHits);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "michelAngle", michelAngle);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "michelBeamAngle", michelBeamAngle);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "muonBeamAngle", muonBeamAngle);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "Completeness", c_PfoCompleteness);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "Purity", c_PfoPurity);
            PandoraMonitoringApi::SetTreeVariable(this->GetPandora(), m_treeName.c_str(), "Efficiency", c_PfoEfficiency);
            PandoraMonitoringApi::FillTree(this->GetPandora(), m_treeName.c_str());
        }
  
            
        
        return STATUS_CODE_SUCCESS;
    }
    
    
    LArMCParticleHelper::PrimaryParameters ParticleHierarchyValidationAlgorithm::CreatePrimaryParameters(){
        LArMCParticleHelper::PrimaryParameters primaryParameters = LArMCParticleHelper::PrimaryParameters();
        primaryParameters.m_minPrimaryGoodHits = 15;
        primaryParameters.m_minHitsForGoodView = 5;
        primaryParameters.m_minPrimaryGoodViews = 2;
        primaryParameters.m_selectInputHits = true;
        primaryParameters.m_maxPhotonPropagation = 2.5f;
        primaryParameters.m_minHitSharingFraction = 0.9f;
        primaryParameters.m_foldBackHierarchy = true;
        return primaryParameters;
    }
    
    
    void ParticleHierarchyValidationAlgorithm::getPurityAndCompleteness(const PfoList *const pPfoList, const MCParticleList *const pMCParts, const CaloHitList *const pCaloHits, std::vector<std::vector<float>> &pfoPurityCompleteness, LArMCParticleHelper::PrimaryParameters primaryParameters){
        for (const ParticleFlowObject *const pPfo : *pPfoList){
            const MCParticle* bestMatch(nullptr);
            bestMatch = findBestMatch(pPfo, pMCParts, pCaloHits, primaryParameters);
            std::vector<float> results = ParticleHierarchyValidationAlgorithm::purityAndCompleteness(pPfo, bestMatch, pMCParts, pCaloHits, primaryParameters);
            pfoPurityCompleteness.push_back(results);
            //std::cout << "Added to the pfoPurityCompleteness Object" << std::endl; 
        }
    }
    
    
    void ParticleHierarchyValidationAlgorithm::getRelevantPfos(const PfoList *const Pfos, PfoList &relevantPfos, const bool f_addAll, bool &PfoElectronFound){
        for (const ParticleFlowObject *c_pfo : *Pfos)
        {
            if (!f_addAll){
                if (abs(c_pfo->GetParticleId()) == 11){
                    //std::cout << "Found a Reco Electron" << std::endl;
                    relevantPfos.push_back(c_pfo);
                    //std::cout << "Added Electron to 'relevantPfos'" << std::endl;
                    PfoElectronFound = true;
                }
                else if (abs(c_pfo->GetParticleId()) == 12){
                    //std::cout << "Found a Reco Electron-Neutrino" << std::endl;
                }
                else if (abs(c_pfo->GetParticleId()) == 13){
                    //std::cout << "Found a Reco Muon" << std::endl;
                    //relevantPfos.push_back(c_pfo);
                    //std::cout << "Added Muon to 'relevantPfos'" << std::endl;
                }
                else if (abs(c_pfo->GetParticleId()) == 14){
                    //std::cout << "Found a Reco Muon-Neutrino" << std::endl;
                }else{
                    //std::cout << "Found PFO, Id: " << c_pfo->GetParticleId() << std::endl;
                }
                    
            }else{    //Add All Particles, regardless of flavour
                //std::cout << "Found PFO, Id: " << c_pfo->GetParticleId() << std::endl;
                relevantPfos.push_back(c_pfo);
            }
        }
    }
    
    void ParticleHierarchyValidationAlgorithm::getRelevantMCParts(const MCParticleList *const MCParts, MCParticleList &relevantMCParts, const bool f_addAll, bool &MCElectronFound){
        for (const MCParticle *c_mcp : *MCParts)
        {   
            if (!f_addAll){
                if (abs(c_mcp->GetParticleId()) == 11){
                    //std::cout << "Found a MC Electron" << std::endl;
                    relevantMCParts.push_back(c_mcp);
                    //std::cout << "Added Electron to 'relevantMCParts'" << std::endl;
                    MCElectronFound = true;
                }
                else if (abs(c_mcp->GetParticleId()) == 12){
                    //std::cout << "Found a MC Electron-Neutrino" << std::endl;
                }
                else if (abs(c_mcp->GetParticleId()) == 13){
                    //std::cout << "Found a MC Muon" << std::endl;
                    //relevantMCParts.push_back(c_mcp);
                    //std::cout << "Added Muon to 'relevantMCParts'" << std::endl;
                }
                else if (abs(c_mcp->GetParticleId()) == 14){
                    //std::cout << "Found a MC Muon-Neutrino" << std::endl;
                }else{
                    //std::cout << "Found MCP, Id: " << c_mcp->GetParticleId() << std::endl;
                }
                    //Add All Particles
            }else{ 
                //std::cout << "Found MCP, Id: " << c_mcp->GetParticleId() << std::endl;
                relevantMCParts.push_back(c_mcp);
            }
            
        }
    }
    
    void ParticleHierarchyValidationAlgorithm::getViewHits(const ParticleFlowObject *const c_Pfo, int &NUHits, int &NVHits, int &NWHits, const bool f_showAllPfoData){
        ClusterList c_clusterList = c_Pfo->GetClusterList();
        if (f_showAllPfoData)
                std::cout << "   >" << "Number of clusters in PFO " << c_Pfo->GetParticleId() << ": " << c_clusterList.size() << std::endl; 
                
        for (const Cluster *const c_cluster : c_clusterList)
        {

            CaloHitList c_caloHitList;
            c_cluster->GetOrderedCaloHitList().FillCaloHitList(c_caloHitList);
            if (f_showAllPfoData)
                std::cout << "     >" << "Number of hits in this cluster: " << c_cluster->GetNCaloHits() << std::endl;
            
            for (const CaloHit *const c_hit : c_caloHitList)
            {
                
                switch(c_hit->GetHitType())
                {
                    case 4:
                        if (f_showAllPfoData)
                            std::cout << "         >" << "CaloHitView: U" << std::endl;
                        NUHits = NUHits+1;
                        break;
                    case 5:
                        if (f_showAllPfoData)
                            std::cout << "         >" << "CaloHitView: V" << std::endl;
                        NVHits = NVHits+1;
                        break;
                    case 6:
                        if (f_showAllPfoData)
                            std::cout << "         >" << "CaloHitView: W" << std::endl;
                        NWHits = NWHits+1;
                        break;
                    default:
                        break;
                } 
                     
            }
        }
    }
    
    
    
    void ParticleHierarchyValidationAlgorithm::getMCParticleViewHits(const MCParticle *const c_MCPart, int &NUHits, int &NVHits, int &NWHits, const bool f_showAllMCPData, const MCParticleList *MCParts, const CaloHitList *CaloHits){
                
        LArMCParticleHelper::MCRelationMap mcToSelfMap;
        LArMCParticleHelper::CaloHitToMCMap hitToMCMap;
        LArMCParticleHelper::MCContributionMap mcToTrueHitListMap;
        
        LArMCParticleHelper::GetMCToSelfMap(MCParts, mcToSelfMap);
        LArMCParticleHelper::GetMCParticleToCaloHitMatches(CaloHits, mcToSelfMap, hitToMCMap, mcToTrueHitListMap);
        
        for (std::pair c_pair : hitToMCMap)
        {
            if (c_pair.second == c_MCPart)
            {
                switch(c_pair.first->GetHitType())
                {
                    case 4:
                        if (f_showAllMCPData)
                            std::cout << "         >" << "(MCP) CaloHitView: U" << std::endl;
                        NUHits = NUHits+1;
                        break;
                    case 5:
                        if (f_showAllMCPData)
                            std::cout << "         >" << "(MCP) CaloHitView: V" << std::endl;
                        NVHits = NVHits+1;
                        break;
                    case 6:
                        if (f_showAllMCPData)
                            std::cout << "         >" << "(MCP) CaloHitView: W" << std::endl;
                        NWHits = NWHits+1;
                        break;
                    default:
                        break;
                } 
            }
        }
    }
    
    
    
    std::vector<float> ParticleHierarchyValidationAlgorithm::purityAndCompleteness(const ParticleFlowObject *const pPfo, const MCParticle* bestMCParticle, const MCParticleList *const MCParts, const CaloHitList *const CaloHits, LArMCParticleHelper::PrimaryParameters &primaryParameters){
        const PfoList myPfoList(1, pPfo);
            
        LArMCParticleHelper::MCContributionMap targetMCParticleToHitsMap;
        LArMCParticleHelper::SelectReconstructableMCParticles(MCParts, CaloHits, primaryParameters, LArMCParticleHelper::IsVisible, targetMCParticleToHitsMap);

        LArMCParticleHelper::MCContributionMapVector mcParticlesToGoodHitsMaps({targetMCParticleToHitsMap});

        LArMCParticleHelper::PfoContributionMap pfoToReconstructable2DHitsMap;
        LArMCParticleHelper::GetPfoToReconstructable2DHitsMap(myPfoList, mcParticlesToGoodHitsMaps, pfoToReconstructable2DHitsMap, primaryParameters.m_foldBackHierarchy);

        LArMCParticleHelper::PfoToMCParticleHitSharingMap pfoToMCParticleHitSharingMap;
        LArMCParticleHelper::MCParticleToPfoHitSharingMap mcParticleToPfoHitSharingMap;
        LArMCParticleHelper::GetPfoMCParticleHitSharingMaps(pfoToReconstructable2DHitsMap, mcParticlesToGoodHitsMaps, pfoToMCParticleHitSharingMap, mcParticleToPfoHitSharingMap);             
            
        const CaloHitList &allHitsInPfo(pfoToReconstructable2DHitsMap.at(pPfo));
	    const int nHitsInPfoTotal(allHitsInPfo.size());
	    int nHitsInBestMCParticleTotal(-1);
	    int nHitsSharedWithBestMCParticleTotal(-1);
            
	    const LArMCParticleHelper::MCParticleToSharedHitsVector &mcParticleToSharedHitsVector(pfoToMCParticleHitSharingMap.at(pPfo));
        
        for (const LArMCParticleHelper::MCParticleCaloHitListPair &mcParticleCaloHitListPair : mcParticleToSharedHitsVector)
	    {     
	        if (mcParticleCaloHitListPair.first == bestMCParticle)
	        {
	            const CaloHitList &associatedMCHits(mcParticleCaloHitListPair.second);
	            const CaloHitList &allMCHits(targetMCParticleToHitsMap.at(bestMCParticle)); 
	            nHitsSharedWithBestMCParticleTotal = associatedMCHits.size();
		        nHitsInBestMCParticleTotal = allMCHits.size();
	        }
	    }	 
	    
        const float completeness((nHitsInBestMCParticleTotal > 0) ? static_cast<float>(nHitsSharedWithBestMCParticleTotal) / static_cast<float>(nHitsInBestMCParticleTotal) : 0.f);
        const float purity((nHitsInPfoTotal > 0 ) ? static_cast<float>(nHitsSharedWithBestMCParticleTotal) / static_cast<float>(nHitsInPfoTotal) : 0.f);
        
        std::vector<float> return_vec = {completeness, purity, (float)nHitsSharedWithBestMCParticleTotal};
        return return_vec;
    }
    
    
    
    
    
    
    
    /*    WORKING
    
    std::vector<float> ParticleHierarchyValidationAlgorithm::purityAndCompleteness(const ParticleFlowObject *const pPfo, const MCParticleList *const pMCParts, const CaloHitList *const CaloHits, LArMCParticleHelper::PrimaryParameters &primaryParameters){
        const PfoList myPfoList(1, pPfo);
        
        //std::cout << "  --Entered for loop--" << std::endl;
            
        LArMCParticleHelper::MCContributionMap targetMCParticleToHitsMap;
        LArMCParticleHelper::SelectReconstructableMCParticles(pMCParts, CaloHits, primaryParameters, LArMCParticleHelper::IsLeadingBeamParticle, targetMCParticleToHitsMap);
            
        //std::cout << "*Size 1: " << MCParts->size() << std::endl;
        //std::cout << "*Size 2: " << CaloHits->size() << std::endl;
        //std::cout << "*Size 3: " << primaryParameters << std::endl;
        //std::cout << "*Size 4: " << LArMCParticleHelper::IsBeamNeutrinoFinalState << std::endl;
        //std::cout << "*Size 5: " << targetMCParticleToHitsMap.size() << std::endl;

        LArMCParticleHelper::MCContributionMapVector mcParticlesToGoodHitsMaps({targetMCParticleToHitsMap});

        LArMCParticleHelper::PfoContributionMap pfoToReconstructable2DHitsMap;
        LArMCParticleHelper::GetPfoToReconstructable2DHitsMap(myPfoList, mcParticlesToGoodHitsMaps, pfoToReconstructable2DHitsMap, primaryParameters.m_foldBackHierarchy);

        LArMCParticleHelper::PfoToMCParticleHitSharingMap pfoToMCParticleHitSharingMap;
        LArMCParticleHelper::MCParticleToPfoHitSharingMap mcParticleToPfoHitSharingMap;
        LArMCParticleHelper::GetPfoMCParticleHitSharingMaps(pfoToReconstructable2DHitsMap, mcParticlesToGoodHitsMaps, pfoToMCParticleHitSharingMap, mcParticleToPfoHitSharingMap);  //#!!!!!#

                        
        //std::cout << "  --Finished Calling LArMCParticleHelper functions--" << std::endl;            
            
        const CaloHitList &allHitsInPfo(pfoToReconstructable2DHitsMap.at(pPfo));
	    const int nHitsInPfoTotal(allHitsInPfo.size());
	    int nHitsInBestMCParticleTotal(-1);
	    int nHitsSharedWithBestMCParticleTotal(-1);
	    float sharedHits(-1);	        
	    //std::cout << "Size 1: " << pfoToReconstructable2DHitsMap.size() << std::endl;
        //std::cout << "Size 2: " << mcParticlesToGoodHitsMaps.size() << std::endl;
        //std::cout << "Size 3: " << pfoToMCParticleHitSharingMap.size() << std::endl;
        //std::cout << "Size 4: " << mcParticleToPfoHitSharingMap.size() << std::endl;
            
	    const LArMCParticleHelper::MCParticleToSharedHitsVector &mcParticleToSharedHitsVector(pfoToMCParticleHitSharingMap.at(pPfo));
	        
	    for (const LArMCParticleHelper::MCParticleCaloHitListPair &mcParticleCaloHitListPair : mcParticleToSharedHitsVector)
	    {
	            
	        //std::cout << "      --In second for loop--" << std::endl;
	            
	        const pandora::MCParticle *const pAssociatedMCParticle(mcParticleCaloHitListPair.first);
	        const CaloHitList &allMCHits(targetMCParticleToHitsMap.at(pAssociatedMCParticle));
	        const CaloHitList &associatedMCHits(mcParticleCaloHitListPair.second);

	        if (static_cast<int>(associatedMCHits.size()) > nHitsSharedWithBestMCParticleTotal)
	        {
		        nHitsSharedWithBestMCParticleTotal = associatedMCHits.size();
		        sharedHits = associatedMCHits.size();
		        nHitsInBestMCParticleTotal = allMCHits.size();               
	        }
	    }		

        std::cout << "      --Calculating completeness and purity--" << std::endl;

        std::cout << "nHitsSharedWithBestMCParticleTotal: " << nHitsSharedWithBestMCParticleTotal << std::endl;
        std::cout << "nHitsInBestMCParticleTotal: " << nHitsInBestMCParticleTotal << std::endl;

        const float completeness((nHitsInBestMCParticleTotal > 0) ? static_cast<float>(nHitsSharedWithBestMCParticleTotal) / static_cast<float>(nHitsInBestMCParticleTotal) : 0.f);
            
        std::cout << "nHitsInPfoTotal: " << nHitsInPfoTotal << std::endl;
            
        const float purity((nHitsInPfoTotal > 0 ) ? static_cast<float>(nHitsSharedWithBestMCParticleTotal) / static_cast<float>(nHitsInPfoTotal) : 0.f);
            
        std::cout << "Completeness: " << completeness << std::endl;
        std::cout << "Purity: " << purity << std::endl; //For single Muon events, purity must be 1, as there are no other particles that can be mixed up *(Maybe)
        
        std::vector<float> return_vec = {completeness, purity, sharedHits};
        
        return return_vec;
    }
    
    */
    
    
    
    
    
    
    
    
    
    
    
    
    
    const pandora::MCParticle* ParticleHierarchyValidationAlgorithm::findBestMatch(const ParticleFlowObject *const pPfo, const MCParticleList *const MCParts, const CaloHitList *const CaloHits, LArMCParticleHelper::PrimaryParameters &primaryParameters)
    {
        const PfoList myPfoList(1, pPfo);
            
        LArMCParticleHelper::MCContributionMap targetMCParticleToHitsMap;
        LArMCParticleHelper::SelectReconstructableMCParticles(MCParts, CaloHits, primaryParameters, LArMCParticleHelper::IsVisible, targetMCParticleToHitsMap);

        LArMCParticleHelper::MCContributionMapVector mcParticlesToGoodHitsMaps({targetMCParticleToHitsMap});

        LArMCParticleHelper::PfoContributionMap pfoToReconstructable2DHitsMap;
        LArMCParticleHelper::GetPfoToReconstructable2DHitsMap(myPfoList, mcParticlesToGoodHitsMaps, pfoToReconstructable2DHitsMap, primaryParameters.m_foldBackHierarchy);

        LArMCParticleHelper::PfoToMCParticleHitSharingMap pfoToMCParticleHitSharingMap;
        LArMCParticleHelper::MCParticleToPfoHitSharingMap mcParticleToPfoHitSharingMap;
        LArMCParticleHelper::GetPfoMCParticleHitSharingMaps(pfoToReconstructable2DHitsMap, mcParticlesToGoodHitsMaps, pfoToMCParticleHitSharingMap, mcParticleToPfoHitSharingMap);  

	    int nHitsSharedWithBestMCParticleTotal(-1);
            
	    const LArMCParticleHelper::MCParticleToSharedHitsVector &mcParticleToSharedHitsVector(pfoToMCParticleHitSharingMap.at(pPfo));

        const pandora::MCParticle* bestMCParticleMatch(nullptr);
	    for (const LArMCParticleHelper::MCParticleCaloHitListPair &mcParticleCaloHitListPair : mcParticleToSharedHitsVector)
	    {     
	        const CaloHitList &associatedMCHits(mcParticleCaloHitListPair.second);

	        if (static_cast<int>(associatedMCHits.size()) > nHitsSharedWithBestMCParticleTotal)
	        {
		        nHitsSharedWithBestMCParticleTotal = associatedMCHits.size();
		        bestMCParticleMatch = mcParticleCaloHitListPair.first;    
	        }
	    }		
        
        return bestMCParticleMatch;
    }
    
    
    
    StatusCode ParticleHierarchyValidationAlgorithm::ReadSettings(const pandora::TiXmlHandle xmlHandle){
        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "MinPrimaryGoodHits", m_primaryParameters.m_minPrimaryGoodHits));

        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "MinHitsForGoodView", m_primaryParameters.m_minHitsForGoodView));

        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "MinPrimaryGoodViews", m_primaryParameters.m_minPrimaryGoodViews));

        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "SelectInputHits", m_primaryParameters.m_selectInputHits));

        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "MinHitSharingFraction", m_primaryParameters.m_minHitSharingFraction));

        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "MaxPhotonPropagation", m_primaryParameters.m_maxPhotonPropagation));

        PANDORA_RETURN_RESULT_IF_AND_IF(STATUS_CODE_SUCCESS, STATUS_CODE_NOT_FOUND, !=, XmlHelper::ReadValue(xmlHandle,
            "FoldToPrimaries", m_primaryParameters.m_foldBackHierarchy));
        return STATUS_CODE_SUCCESS;
    }
    
}
