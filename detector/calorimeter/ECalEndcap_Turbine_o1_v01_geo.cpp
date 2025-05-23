#include "DD4hep/DetFactoryHelper.h"
#include "DD4hep/Printout.h"
#include "TMatrixT.h"
#include "XML/Utilities.h"
#include <DDRec/DetectorData.h>

namespace det {

unsigned ECalEndCapElementCounter = 0;

unsigned ECalEndcapNumCalibLayers;

double tForArcLength(double s, double bladeangle, double delZ, double r) {

  // some intermediate constants
  double zpos = delZ / 2.;
  double zp = zpos / TMath::Tan(bladeangle);
  double b = zp / (TMath::Sqrt(r * r - zp * zp));
  double c = (TMath::Tan(s / r) + b) / (1. - b * TMath::Tan(s / r));
  double d = c * c * r * r / (1 + c * c);
  return (TMath::Sqrt(d) - zp) * TMath::Sin(bladeangle);
}

// return position of the inner edge of a blade
double getZmin(double r, double bladeangle, double delZ) {
  // r: distance from the beamline
  // bladeangle: angle of turbine blades wrt xy plane, in radians
  // delZ: z extent of the blades
  return TMath::Sqrt(r * r - ((delZ / 2) / TMath::Tan(bladeangle)) * ((delZ / 2) / TMath::Tan(bladeangle)));
}

dd4hep::Solid buildOneBlade(double thickness_inner, double thickness_outer, double width, double ro, double ri,
                            double bladeangle, double delZ) {

  dd4hep::Solid shapeBeforeSubtraction;

  // set max and min extent of the blade (along the z axis in the body frame)
  double zmax = ro;
  double zmin = getZmin(ri, bladeangle, delZ);

  dd4hep::Trd2 tmp1(thickness_inner / 2., thickness_outer / 2., width / 2., width / 2., (zmax - zmin) / 2.);
  shapeBeforeSubtraction = tmp1;

  dd4hep::Tube allowedTube(ri, ro, delZ);

  return dd4hep::IntersectionSolid(
      shapeBeforeSubtraction, allowedTube,
      dd4hep::Transform3D(dd4hep::RotationZYX(0, TMath::Pi() / 2. - bladeangle, TMath::Pi() / 2.),
                          dd4hep::Position(0, 0, -(zmin + zmax) / 2.)));
}

void buildWheel(dd4hep::Detector& aLcdd, dd4hep::SensitiveDetector& aSensDet, dd4hep::Volume& aEnvelope,
                dd4hep::xml::Handle_t& aXmlElement, dd4hep::DetElement& bathDetElem, float ri, float ro, float delZ,
                unsigned iWheel) {

  dd4hep::xml::DetElement calorimeterElem = aXmlElement.child(_Unicode(calorimeter));
  dd4hep::xml::DetElement genericBladeElem = calorimeterElem.child(_Unicode(turbineBlade));
  dd4hep::xml::DetElement absBladeElem = genericBladeElem.child(_Unicode(absorberBlade));
  dd4hep::xml::DetElement claddingElem = genericBladeElem.child(_Unicode(cladding));
  dd4hep::xml::DetElement glueElem = genericBladeElem.child(_Unicode(glue));
  dd4hep::xml::DetElement electrodeBladeElem = genericBladeElem.child(_Unicode(electrodeBlade));
  dd4hep::xml::DetElement nobleLiquidElem = genericBladeElem.child(_Unicode(nobleLiquidGap));

  float BladeAngle = genericBladeElem.attr<float>(_Unicode(angle));
  bool decreaseAnglePerWheel = genericBladeElem.attr<bool>(_Unicode(decreaseAnglePerWheel));
  dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "Making wheel with inner, outer radii %f, %f", ri, ro);

  dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "Blade angle is %f; decrease angle per wheel? ",
                   BladeAngle, decreaseAnglePerWheel);
  dd4hep::xml::Dimension dim(aXmlElement.child(_Unicode(dimensions)));
  double grmin = dim.rmin1();
  dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "delZ is %f", delZ);

  if (decreaseAnglePerWheel) {
    float tubeFracCovered = delZ / (2 * grmin * TMath::Tan(BladeAngle));
    BladeAngle = TMath::ATan(delZ / (2 * ri * tubeFracCovered));
  }

  if (TMath::Abs(TMath::Tan(BladeAngle)) < delZ / (2. * ri)) {
    dd4hep::printout(dd4hep::ERROR, "ECalEndcap_Turbine_o1_v01",
                     "The requested blade angle is too small for the given delZ and ri values.  Please adjust to at "
                     "least %f degrees!",
                     TMath::ATan(delZ / (2. * ri)) * 180. / TMath::Pi());

    return;
  }

  Float_t xRange = delZ / (TMath::Sin(BladeAngle));

  double delrPhiNoGap;

  float GlueThick = glueElem.attr<float>(_Unicode(thickness));
  float CladdingThick = claddingElem.attr<float>(_Unicode(thickness));
  float AbsThickMin = absBladeElem.attr<float>(_Unicode(thickness)) - (GlueThick + CladdingThick);
  if (AbsThickMin < 0.) {
    dd4hep::printout(
        dd4hep::ERROR, "ECalEndcap_Turbine_o1_v01",
        "Error: requested absorber thickness is negative after accounting for glue and cladding thickness");
  }
  float ElectrodeThick = electrodeBladeElem.attr<float>(_Unicode(thickness));
  float LArgapi = nobleLiquidElem.attr<float>(_Unicode(gap));

  bool sameNUnitCells = genericBladeElem.attr<bool>(_Unicode(sameNUnitCells));
  auto nUnitCellsStrArr = genericBladeElem.attr<std::string>(_Unicode(nUnitCells));
  char* nUnitCellsCStr = strtok(const_cast<char*>(nUnitCellsStrArr.c_str()), " ");
  int nUnitCells = 0;
  if (!sameNUnitCells) {
    for (unsigned i = 0; i < iWheel; i++) {
      nUnitCellsCStr = strtok(NULL, " ");
    }
    std::string nUnitCellsStr = nUnitCellsCStr;
    nUnitCells = std::stoi(nUnitCellsStr);
  }
  int nUnitCellsLeastCommonMultiple = genericBladeElem.attr<int>(_Unicode(nUnitCellsLeastCommonMultiple));

  bool scaleBladeThickness = absBladeElem.attr<bool>(_Unicode(scaleThickness));
  float bladeThicknessScaleFactor = absBladeElem.attr<float>(_Unicode(thicknessScaleFactor));

  dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "nUnitCells: %d", nUnitCells);

  float AbsThicki = AbsThickMin;
  // make volumes for the noble liquid, electrode, and absorber blades
  float AbsThicko;
  if (scaleBladeThickness) {
    AbsThicko = AbsThicki + bladeThicknessScaleFactor * ((ro / ri) - 1.) * AbsThicki;
  } else {
    AbsThicko = AbsThicki;
  }

  // Calculate gap thickness at inner layer
  double circ = 2 * TMath::Pi() * ri;
  double x2 = (AbsThickMin + (GlueThick + CladdingThick) + ElectrodeThick) / TMath::Sin(BladeAngle);
  double y2 = TMath::Sqrt(ri * ri - x2 * x2);
  double rPhi1 = ri * TMath::Pi() / 2.;
  double rPhi2 = ri * TMath::ATan(y2 / x2);
  delrPhiNoGap = TMath::Abs(rPhi1 - rPhi2);
  double leftoverS = (circ - nUnitCells * delrPhiNoGap);
  double delrPhiGapOnly = leftoverS / (2 * nUnitCells);
  LArgapi = delrPhiGapOnly * TMath::Sin(BladeAngle);
  dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "LArGap at inner radius is %f", LArgapi);

  // now find gap at outer radius
  circ = 2 * TMath::Pi() * ro;
  x2 = (AbsThicko + GlueThick + CladdingThick + ElectrodeThick) / TMath::Sin(BladeAngle);
  y2 = TMath::Sqrt(ro * ro - x2 * x2);
  rPhi1 = ro * TMath::Pi() / 2.;
  rPhi2 = ro * TMath::ATan(y2 / x2);
  delrPhiNoGap = TMath::Abs(rPhi1 - rPhi2);
  leftoverS = (circ - nUnitCells * delrPhiNoGap);
  delrPhiGapOnly = leftoverS / (2 * nUnitCells);
  float LArgapo = delrPhiGapOnly * TMath::Sin(BladeAngle);

  float riLayer = ri;

  std::vector<dd4hep::Volume> claddingLayerVols;
  std::vector<dd4hep::Volume> glueLayerVols;
  std::vector<dd4hep::Volume> absBladeLayerVols;
  std::vector<dd4hep::Volume> LArTotalLayerVols;
  std::vector<dd4hep::Volume> electrodeBladeLayerVols;

  dd4hep::Solid passiveShape = buildOneBlade(AbsThicki + GlueThick + CladdingThick,
                                             AbsThicko + GlueThick + CladdingThick, xRange, ro, ri, BladeAngle, delZ);
  dd4hep::Volume passiveVol("passive", passiveShape, aLcdd.material("Air"));

  dd4hep::Solid activeShape =
      buildOneBlade(ElectrodeThick + LArgapi * 2, ElectrodeThick + LArgapo * 2, xRange, ro, ri, BladeAngle, delZ);
  dd4hep::Volume activeVol("active", activeShape, aLcdd.material("Air"));

  unsigned numNonActiveLayers = 1;
  // check that either all non-active volumes are set to sensitive (for
  // sampling fraction calculations) or none are (for normal running)
  bool allNonActiveSensitive = (claddingElem.isSensitive() && glueElem.isSensitive() && absBladeElem.isSensitive() &&
                                electrodeBladeElem.isSensitive());
  bool allNonActiveNotSensitive = (!claddingElem.isSensitive() && !glueElem.isSensitive() &&
                                   !absBladeElem.isSensitive() && !electrodeBladeElem.isSensitive());
  if (allNonActiveSensitive) {
    numNonActiveLayers = ECalEndcapNumCalibLayers;
  } else if (allNonActiveNotSensitive) {
    numNonActiveLayers = 1;
  } else {
    dd4hep::printout(dd4hep::ERROR, "ECalEndcap_Turbine_o1_v01",
                     "Some non-active layers are sensitive and others are not -- this is likely a misconfiguration");
  }

  float delrNonActive = (ro - ri) / numNonActiveLayers;
  float delrActive = (ro - ri) / ECalEndcapNumCalibLayers;

  for (unsigned iLayer = 0; iLayer < numNonActiveLayers; iLayer++) {
    float roLayer = riLayer + delrNonActive;
    dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Making layer with inner, outer radii %f, %f", riLayer,
                     roLayer);

    if (scaleBladeThickness) {
      AbsThicko = AbsThicki + bladeThicknessScaleFactor * ((roLayer / riLayer) - 1.) * AbsThicki;
    } else {
      AbsThicko = AbsThicki;
    }
    dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "Inner and outer absorber thicknesses %f, %f ",
                     AbsThicki, AbsThicko);

    dd4hep::Solid claddingLayer =
        buildOneBlade(AbsThicki + GlueThick + CladdingThick, AbsThicko + GlueThick + CladdingThick, xRange, roLayer,
                      riLayer, BladeAngle, delZ);

    dd4hep::Solid glueLayer =
        buildOneBlade(AbsThicki + GlueThick, AbsThicko + GlueThick, xRange, roLayer, riLayer, BladeAngle, delZ);

    dd4hep::Solid absBladeLayer = buildOneBlade(AbsThicki, AbsThicko, xRange, roLayer, riLayer, BladeAngle, delZ);

    dd4hep::Volume claddingLayerVol("claddingLayer", claddingLayer, aLcdd.material(claddingElem.materialStr()));
    if (claddingElem.isSensitive()) {
      claddingLayerVol.setSensitiveDetector(aSensDet);
    }
    claddingLayerVols.push_back(claddingLayerVol);

    dd4hep::Volume glueLayerVol("glueLayer", glueLayer, aLcdd.material(glueElem.materialStr()));
    if (glueElem.isSensitive()) {
      glueLayerVol.setSensitiveDetector(aSensDet);
    }
    glueLayerVols.push_back(glueLayerVol);

    dd4hep::Volume absBladeLayerVol("absBladeLayer", absBladeLayer, aLcdd.material(absBladeElem.materialStr()));
    if (absBladeElem.isSensitive()) {
      absBladeLayerVol.setSensitiveDetector(aSensDet);
    }
    absBladeLayerVols.push_back(absBladeLayerVol);

    riLayer = roLayer;
    AbsThicki = AbsThicko;
  }

  riLayer = ri;

  AbsThicki = AbsThickMin;

  for (unsigned iLayer = 0; iLayer < ECalEndcapNumCalibLayers; iLayer++) {

    float roLayer = riLayer + delrActive;

    if (scaleBladeThickness) {
      AbsThicko = AbsThicki + bladeThicknessScaleFactor * ((roLayer / riLayer) - 1.) * AbsThicki;
    } else {
      AbsThicko = AbsThicki;
    }

    // now find gap at outer layer
    circ = 2 * TMath::Pi() * roLayer;
    x2 = (AbsThicko + GlueThick + CladdingThick + ElectrodeThick) / TMath::Sin(BladeAngle);
    y2 = TMath::Sqrt(roLayer * roLayer - x2 * x2);
    rPhi1 = roLayer * TMath::Pi() / 2.;
    rPhi2 = roLayer * TMath::ATan(y2 / x2);
    delrPhiNoGap = TMath::Abs(rPhi1 - rPhi2);
    leftoverS = (circ - nUnitCells * delrPhiNoGap);
    delrPhiGapOnly = leftoverS / (2 * nUnitCells);
    LArgapo = delrPhiGapOnly * TMath::Sin(BladeAngle);
    dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "Outer LAr gap is %f", LArgapo);
    dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                     "Inner and outer thicknesses of noble liquid volume %f, %f", ElectrodeThick + LArgapi * 2,
                     ElectrodeThick + LArgapo * 2);

    dd4hep::Solid electrodeBladeAndGapLayer = buildOneBlade(ElectrodeThick + LArgapi * 2, ElectrodeThick + LArgapo * 2,
                                                            xRange, roLayer, riLayer, BladeAngle, delZ);

    dd4hep::Solid electrodeBladeLayer =
        buildOneBlade(ElectrodeThick, ElectrodeThick, xRange, roLayer, riLayer, BladeAngle, delZ);

    dd4hep::Volume electrodeBladeLayerVol("electrodeBladeLayer", electrodeBladeLayer,
                                          aLcdd.material(electrodeBladeElem.materialStr()));
    if (electrodeBladeElem.isSensitive()) {
      electrodeBladeLayerVol.setSensitiveDetector(aSensDet);
    }
    electrodeBladeLayerVols.push_back(electrodeBladeLayerVol);

    dd4hep::Volume LArTotalLayerVol("LArTotalLayerVol", electrodeBladeAndGapLayer,
                                    aLcdd.material(nobleLiquidElem.materialStr()));

    if (nobleLiquidElem.isSensitive()) {
      LArTotalLayerVol.setSensitiveDetector(aSensDet);
    }
    LArTotalLayerVols.push_back(LArTotalLayerVol);

    riLayer = roLayer;
    LArgapi = LArgapo;
    AbsThicki = AbsThicko;
  }
  dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                   "ECal endcap materials:  nobleLiquid: %s absorber %s electrode %s",
                   nobleLiquidElem.materialStr().c_str(), absBladeElem.materialStr().c_str(),
                   electrodeBladeElem.materialStr().c_str());

  int nUnitCellsToDraw = nUnitCells;

  dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Number of unit cells %d", nUnitCells);

  // place all components of the absorber blade inside passive volume

  unsigned iLayer = 0;

  riLayer = ri;

  for (auto absBladeLayerVol : absBladeLayerVols) {

    float roLayer = riLayer + delrNonActive;

    dd4hep::Position posLayer(0, 0, (riLayer - ri + roLayer - ro) / 2.);
    dd4hep::PlacedVolume absBladeVol_pv = glueLayerVols[iLayer].placeVolume(absBladeLayerVol, posLayer);

    absBladeVol_pv.addPhysVolID("subtype", 0); // 0 = absorber, 1 = glue, 2 = cladding
    dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01_geo", "Blade layer, rho is %d, %f, %f", iLayer,
                     absBladeVol_pv.position().Rho(), roLayer / 2.);
    absBladeVol_pv.addPhysVolID("layer", iWheel * numNonActiveLayers + iLayer);

    riLayer = roLayer;
    iLayer++;
  }

  riLayer = ri;
  iLayer = 0;

  for (auto glueLayerVol : glueLayerVols) {

    float roLayer = riLayer + delrNonActive;

    dd4hep::Position posLayer(0, 0, (riLayer - ri + roLayer - ro) / 2.);
    dd4hep::PlacedVolume glueVol_pv = claddingLayerVols[iLayer].placeVolume(glueLayerVol, posLayer);

    glueVol_pv.addPhysVolID("subtype", 1); // 0 = absorber, 1 = glue, 2 = cladding
    glueVol_pv.addPhysVolID("layer", iWheel * numNonActiveLayers + iLayer);

    riLayer = roLayer;
    iLayer++;
  }

  riLayer = ri;
  iLayer = 0;

  double zminri = getZmin(ri, BladeAngle, delZ);

  for (auto claddingLayerVol : claddingLayerVols) {

    float roLayer = riLayer + delrNonActive;

    double zminLayer = getZmin(riLayer, BladeAngle, delZ);

    dd4hep::Position posLayer(0, 0, (zminLayer - zminri + roLayer - ro) / 2.);
    dd4hep::PlacedVolume claddingVol_pv = passiveVol.placeVolume(claddingLayerVol, posLayer);

    claddingVol_pv.addPhysVolID("subtype", 2); // 0 = absorber, 1 = glue, 2 = cladding
    claddingVol_pv.addPhysVolID("layer", iWheel * numNonActiveLayers + iLayer);

    riLayer = roLayer;
    iLayer++;
  }

  riLayer = ri;
  iLayer = 0;

  for (auto electrodeBladeLayerVol : electrodeBladeLayerVols) {

    float roLayer = riLayer + delrActive;

    dd4hep::PlacedVolume electrodeBladeVol_pv = LArTotalLayerVols[iLayer].placeVolume(electrodeBladeLayerVol);
    electrodeBladeVol_pv.addPhysVolID("layer", iWheel * numNonActiveLayers + iLayer);

    riLayer = roLayer;
    iLayer++;
  }

  riLayer = ri;
  iLayer = 0;

  for (auto LArTotalLayerVol : LArTotalLayerVols) {

    float roLayer = riLayer + delrActive;

    double zminLayer = getZmin(riLayer, BladeAngle, delZ);

    dd4hep::Position posLayer(0, 0, (zminLayer - zminri + roLayer - ro) / 2.);

    dd4hep::PlacedVolume LArVol_pv(activeVol.placeVolume(LArTotalLayerVol, posLayer));
    dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "LAr layer: %d", iLayer);
    LArVol_pv.addPhysVolID("layer", iWheel * ECalEndcapNumCalibLayers + iLayer);

    riLayer = roLayer;
    iLayer++;
  }

  for (int iUnitCell = 0; iUnitCell < nUnitCellsToDraw; iUnitCell++) {

    int modIndex = iUnitCell - nUnitCellsToDraw / 2;
    if (modIndex < 0)
      modIndex += nUnitCells;
    float phi = (iUnitCell - nUnitCellsToDraw / 2) * 2 * TMath::Pi() / nUnitCells;
    float delPhi = 2 * TMath::Pi() / nUnitCells;

    dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "Placing blade, ro, ri = %f %f", ro, ri);
    TGeoRotation tgr;
    tgr.RotateZ(BladeAngle * 180 / TMath::Pi());
    tgr.RotateX(-phi * 180 / TMath::Pi());
    tgr.RotateY(90);

    const Double_t* rotMatPtr;

    rotMatPtr = tgr.GetRotationMatrix();
    TMatrixT<Double_t> rotMat(3, 3, rotMatPtr);
    dd4hep::Rotation3D r3d;
    r3d.SetComponents(rotMat(0, 0), rotMat(0, 1), rotMat(0, 2), rotMat(1, 0), rotMat(1, 1), rotMat(1, 2), rotMat(2, 0),
                      rotMat(2, 1), rotMat(2, 2));

    tgr.Clear();
    tgr.RotateZ(BladeAngle * 180 / TMath::Pi());
    tgr.RotateX(-(phi + delPhi / 2.) * 180 / TMath::Pi());
    tgr.RotateY(90);

    rotMatPtr = tgr.GetRotationMatrix();
    TMatrixT<Double_t> rotMat2(3, 3, rotMatPtr);
    dd4hep::Rotation3D r3d2;
    r3d2.SetComponents(rotMat2(0, 0), rotMat2(0, 1), rotMat2(0, 2), rotMat2(1, 0), rotMat2(1, 1), rotMat2(1, 2),
                       rotMat2(2, 0), rotMat2(2, 1), rotMat2(2, 2));

    riLayer = ri;

    float xCell = ((ro + zminri) / 2.) * TMath::Cos(phi);
    float yCell = ((ro + zminri) / 2.) * TMath::Sin(phi);
    float zCell = 0.;

    dd4hep::Transform3D comCell(r3d, dd4hep::Translation3D(xCell, yCell, zCell));

    // place passive volume in LAr bath
    dd4hep::PlacedVolume passivePhysVol = aEnvelope.placeVolume(passiveVol, comCell);
    passivePhysVol.addPhysVolID("module", modIndex * nUnitCellsLeastCommonMultiple / nUnitCells);
    passivePhysVol.addPhysVolID("wheel", iWheel);
    passivePhysVol.addPhysVolID("type", 1); // 0 = active, 1 = passive, 2 = readout
    dd4hep::DetElement passiveDetElem(
        bathDetElem, "passive_" + std::to_string(iUnitCell) + "_" + std::to_string(iWheel), ECalEndCapElementCounter++);
    passiveDetElem.setPlacement(passivePhysVol);

    // place active volume in LAr bath
    xCell = ((ro + zminri) / 2.) * TMath::Cos(phi + delPhi / 2.);
    yCell = ((ro + zminri) / 2.) * TMath::Sin(phi + delPhi / 2.);
    zCell = 0.;
    dd4hep::Transform3D comCell2(r3d2, dd4hep::Translation3D(xCell, yCell, zCell));
    dd4hep::PlacedVolume activePhysVol = aEnvelope.placeVolume(activeVol, comCell2);
    activePhysVol.addPhysVolID("module", modIndex * nUnitCellsLeastCommonMultiple / nUnitCells);
    activePhysVol.addPhysVolID("wheel", iWheel);
    activePhysVol.addPhysVolID("type", 0); // 0 = active, 1 = passive, 2 = readout

    dd4hep::DetElement activeDetElem(bathDetElem, "active_" + std::to_string(iUnitCell) + "_" + std::to_string(iWheel),
                                     ECalEndCapElementCounter++);
    activeDetElem.setPlacement(activePhysVol);

    riLayer = ri;
    iLayer = 0;

    dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "LArTotalLayerVols.size = %d",
                     LArTotalLayerVols.size());
  }

  return;
}

void buildOneSide_Turbine(dd4hep::Detector& aLcdd, dd4hep::SensitiveDetector& aSensDet, dd4hep::Volume& aEnvelope,
                          dd4hep::xml::Handle_t& aXmlElement, unsigned& iModule) {

  dd4hep::xml::DetElement calo = aXmlElement.child(_Unicode(calorimeter));
  dd4hep::xml::Dimension caloDim(calo.dimensions());

  dd4hep::xml::DetElement blade = calo.child(_Unicode(turbineBlade));
  dd4hep::xml::DetElement nobleLiquid = blade.child(_Unicode(nobleLiquidGap));

  dd4hep::xml::DetElement xmlDetElem = aXmlElement;
  std::string nameDet = xmlDetElem.nameStr();
  dd4hep::DetElement caloDetElem(nameDet, xmlDetElem.id());

  dd4hep::xml::Dimension dim(aXmlElement.child(_Unicode(dimensions)));

  // build cryostat
  //  Retrieve cryostat data
  dd4hep::xml::DetElement cryostat = calo.child(_Unicode(cryostat));
  dd4hep::xml::Dimension cryoDim(cryostat.dimensions());
  double cryoThicknessFront = cryoDim.rmin2() - cryoDim.rmin1();

  dd4hep::xml::DetElement cryoFront = cryostat.child(_Unicode(front));
  dd4hep::xml::DetElement cryoBack = cryostat.child(_Unicode(back));
  dd4hep::xml::DetElement cryoSide = cryostat.child(_Unicode(side));
  bool cryoFrontSensitive = cryoFront.isSensitive();
  bool cryoBackSensitive = cryoBack.isSensitive();
  bool cryoSideSensitive = cryoSide.isSensitive();

  double bathRmin = caloDim.rmin(); // - margin for inclination
  double bathRmax = caloDim.rmax(); // + margin for inclination
  double bathDelZ = caloDim.dz();
  dd4hep::Tube bathOuterShape(bathRmin, bathRmax, bathDelZ); // make it 4 volumes + 5th for detector envelope
  dd4hep::Tube bathAndServicesOuterShape(cryoDim.rmin2(), cryoDim.rmax1(),
                                         caloDim.dz()); // make it 4 volumes + 5th for detector envelope

  dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Cryostat front thickness is %f cm",
                   cryoDim.rmin2() / dd4hep::cm);

  if (cryoThicknessFront > 0) {
    // 1. Create cryostat
    dd4hep::Tube cryoFrontShape(cryoDim.rmin1(), cryoDim.rmin2(), cryoDim.dz());
    dd4hep::Tube cryoBackShape(cryoDim.rmax1(), cryoDim.rmax2(), cryoDim.dz());
    dd4hep::Tube cryoSideOuterShape(cryoDim.rmin2(), cryoDim.rmax1(), cryoDim.dz());
    dd4hep::SubtractionSolid cryoSideShape(cryoSideOuterShape, bathAndServicesOuterShape);
    dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                     "ECAL endcap cryostat: front: rmin (cm) = %f rmax (cm) = %f dz (cm) = %f ", cryoDim.rmin1(),
                     cryoDim.rmin2(), cryoDim.dz());
    dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                     "ECAL endcap cryostat: back: rmin (cm) =  %f rmax (cm) = %f dz (cm) = %f", cryoDim.rmax1(),
                     cryoDim.rmax2(), cryoDim.dz());
    dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                     "ECAL endcap cryostat: side: rmin (cm) =  %f rmax (cm) = %f dz (cm) = %f", cryoDim.rmin2(),
                     cryoDim.rmax1(), cryoDim.dz() - caloDim.dz());
    dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Cryostat is made out of %s",
                     cryostat.materialStr().c_str());

    dd4hep::Volume cryoFrontVol(cryostat.nameStr() + "_front", cryoFrontShape, aLcdd.material(cryostat.materialStr()));
    dd4hep::Volume cryoBackVol(cryostat.nameStr() + "_back", cryoBackShape, aLcdd.material(cryostat.materialStr()));
    dd4hep::Volume cryoSideVol(cryostat.nameStr() + "_side", cryoSideShape, aLcdd.material(cryostat.materialStr()));
    dd4hep::PlacedVolume cryoFrontPhysVol = aEnvelope.placeVolume(cryoFrontVol);
    dd4hep::PlacedVolume cryoBackPhysVol = aEnvelope.placeVolume(cryoBackVol);
    dd4hep::PlacedVolume cryoSidePhysVol = aEnvelope.placeVolume(cryoSideVol);
    unsigned sidetype = 0x4; // probably not needed anymore...
    if (cryoFrontSensitive) {
      cryoFrontVol.setSensitiveDetector(aSensDet);
      cryoFrontPhysVol.addPhysVolID("cryo", 1);
      cryoFrontPhysVol.addPhysVolID("type", sidetype + 1);
      dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Cryostat front volume set as sensitive");
    }
    if (cryoBackSensitive) {
      cryoBackVol.setSensitiveDetector(aSensDet);
      cryoBackPhysVol.addPhysVolID("cryo", 1);
      cryoBackPhysVol.addPhysVolID("type", sidetype + 2);
      dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Cryostat back volume set as sensitive");
    }
    if (cryoSideSensitive) {
      cryoSideVol.setSensitiveDetector(aSensDet);
      cryoSidePhysVol.addPhysVolID("cryo", 1);
      cryoSidePhysVol.addPhysVolID("type", sidetype + 3);
      dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Cryostat front volume set as sensitive");
    }
    dd4hep::DetElement cryoFrontDetElem(caloDetElem, "cryo_front", 0);
    cryoFrontDetElem.setPlacement(cryoFrontPhysVol);
    dd4hep::DetElement cryoBackDetElem(caloDetElem, "cryo_back", 0);
    cryoBackDetElem.setPlacement(cryoBackPhysVol);
    dd4hep::DetElement cryoSideDetElem(caloDetElem, "cryo_side", 0);
    cryoSideDetElem.setPlacement(cryoSidePhysVol);
  }

  // 2. Create noble liquid bath
  std::string nobleLiquidMaterial = nobleLiquid.materialStr();
  dd4hep::Volume bathVol(nobleLiquidMaterial + "_bath", bathOuterShape, aLcdd.material(nobleLiquidMaterial));
  dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                   "ECAL endcap bath: material = %s rmin (cm) = %f rmax (cm) = %f, dz (cm) = %f, thickness in front of "
                   "ECal (cm) = %f,  thickness behind ECal (cm) = %f",
                   nobleLiquidMaterial.c_str(), bathRmin, bathRmax, caloDim.dz(), caloDim.rmin() - cryoDim.rmin2(),
                   cryoDim.rmax1() - caloDim.rmax());

  dd4hep::DetElement bathDetElem(caloDetElem, "bath", 1);

  // 3. Create detector structure
  double length = dim.dz() * 2.;
  double zOffsetEnvelope = -length / 2.;

  dd4hep::xml::DetElement supportTubeElem = calo.child(_Unicode(supportTube));
  unsigned nWheels = supportTubeElem.attr<unsigned>(_Unicode(nWheels));
  dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01", "Will build %d wheels", nWheels);

  double rmin = bathRmin;
  double rmax = bathRmax;
  float radiusRatio = pow(rmax / rmin, 1. / nWheels);
  double ro = rmin * radiusRatio;
  double ri = rmin;

  float supportTubeThickness = supportTubeElem.thickness();

  for (unsigned iWheel = 0; iWheel < nWheels; iWheel++) {

    dd4hep::Tube supportTube(ro, ro + supportTubeThickness, bathDelZ);

    dd4hep::Volume supportTubeVol("supportTube", supportTube, aLcdd.material(supportTubeElem.materialStr()));
    if (supportTubeElem.isSensitive()) {
      supportTubeVol.setSensitiveDetector(aSensDet);
    }
    dd4hep::PlacedVolume supportTube_pv =
        bathVol.placeVolume(supportTubeVol, dd4hep::Position(0, 0, zOffsetEnvelope + dim.dz()));
    supportTube_pv.addPhysVolID("cryo", 1);
    supportTube_pv.addPhysVolID("wheel", iWheel);
    dd4hep::DetElement supportTubeDetElem(bathDetElem, "supportTube_" + std::to_string(iWheel), 0);
    supportTubeDetElem.setPlacement(supportTube_pv);

    buildWheel(aLcdd, aSensDet, bathVol, aXmlElement, bathDetElem, ri + supportTubeThickness, ro, bathDelZ * 2, iWheel);
    ri = ro;
    ro *= radiusRatio;
    if (ro > rmax)
      ro = rmax;
  }

  dd4hep::PlacedVolume bathPhysVol = aEnvelope.placeVolume(bathVol);
  bathDetElem.setPlacement(bathPhysVol);

  dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01", "Total number of modules:  %d", iModule);

  return;
}

static dd4hep::Ref_t createECalEndcapTurbine(dd4hep::Detector& aLcdd, dd4hep::xml::Handle_t aXmlElement,
                                             dd4hep::SensitiveDetector aSensDet) {

  dd4hep::xml::DetElement xmlDetElem = aXmlElement;
  std::string nameDet = xmlDetElem.nameStr();
  int idDet = xmlDetElem.id();
  dd4hep::xml::Dimension dim(xmlDetElem.dimensions());
  dd4hep::DetElement caloDetElem(nameDet, idDet);
  dd4hep::xml::Dimension sdType = xmlDetElem.child(_U(sensitive));
  aSensDet.setType(sdType.typeStr());

  ECalEndcapNumCalibLayers = aLcdd.constant<int>("ECalEndcapNumCalibLayers");

  // Create air envelope for one endcap (will be copied to make both endcaps)
  dd4hep::Tube endcapShape(dim.rmin1(), dim.rmax1(), dim.dz());

  dd4hep::Volume envelopeVol(nameDet + "_vol", endcapShape, aLcdd.material("Air"));

  dd4hep::printout(dd4hep::DEBUG, "ECalEndcap_Turbine_o1_v01",
                   "Placing detector on the positive side: (cm) %f  with min, max radii %f %f", dim.z_offset(),
                   dim.rmin1(), dim.rmax1());

  unsigned iModule = 0;
  buildOneSide_Turbine(aLcdd, aSensDet, envelopeVol, aXmlElement, iModule);

  dd4hep::Assembly endcapsAssembly("ECalEndcaps_turbine");

  // Place the positive endcap
  dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                   "Placing detector on the positive side: (cm) %f  with min, max radii %f %f", dim.z_offset(),
                   dim.rmin1(), dim.rmax1());
  dd4hep::Transform3D envelopePositiveVolume_tr(dd4hep::RotationZYX(0, 0, 0),
                                                dd4hep::Translation3D(0, 0, dim.z_offset()));
  dd4hep::PlacedVolume envelopePositivePhysVol = endcapsAssembly.placeVolume(envelopeVol, envelopePositiveVolume_tr);
  envelopePositivePhysVol.addPhysVolID("side", 1);
  dd4hep::DetElement caloPositiveDetElem(caloDetElem, "positive", 0);
  caloPositiveDetElem.setPlacement(envelopePositivePhysVol);

  // make another placement for the negative z endcap
  dd4hep::printout(dd4hep::INFO, "ECalEndcap_Turbine_o1_v01",
                   "Placing detector on the negative side: (cm) %f  with min, max radii %f %f", -dim.z_offset(),
                   dim.rmin1(), dim.rmax1());
  dd4hep::Transform3D envelopeNegativeVolume_tr(dd4hep::RotationZYX(0, 0, 180 * dd4hep::deg),
                                                dd4hep::Translation3D(0, 0, -dim.z_offset()));
  dd4hep::PlacedVolume envelopeNegativePhysVol = endcapsAssembly.placeVolume(envelopeVol, envelopeNegativeVolume_tr);
  envelopeNegativePhysVol.addPhysVolID("side", -1);
  dd4hep::DetElement caloNegativeDetElem(caloDetElem, "negative", 0);
  caloNegativeDetElem.setPlacement(envelopeNegativePhysVol);

  dd4hep::Volume motherVol = aLcdd.pickMotherVolume(caloDetElem);
  dd4hep::PlacedVolume envelopePhysVol = motherVol.placeVolume(endcapsAssembly);
  caloDetElem.setPlacement(envelopePhysVol);
  envelopePhysVol.addPhysVolID("system", idDet);

  // Create dummy caloData object for PandoraPFA
  // FIXME: fill calo and layer data information
  auto caloData = new dd4hep::rec::LayeredCalorimeterData;
  caloData->layoutType = dd4hep::rec::LayeredCalorimeterData::EndcapLayout;
  caloDetElem.addExtension<dd4hep::rec::LayeredCalorimeterData>(caloData);

  // save extent information
  // GM: this is the envelope - maybe save the bath dimensions instead?
  caloData->extent[0] = dim.rmin1();
  caloData->extent[1] = dim.rmax1();
  caloData->extent[2] = dim.z_offset() - dim.dz();
  caloData->extent[3] = dim.z_offset() + dim.dz();

  // Set type flags
  dd4hep::xml::setDetectorTypeFlag(xmlDetElem, caloDetElem);

  return caloDetElem;
}
} // namespace det

DECLARE_DETELEMENT(ECalEndcap_Turbine_o1_v01, det::createECalEndcapTurbine)
