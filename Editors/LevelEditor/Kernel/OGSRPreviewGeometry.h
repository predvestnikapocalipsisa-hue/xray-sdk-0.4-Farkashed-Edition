#pragma once

// Preview instances are rigid snapshots, including objects which retain bones
// after being placed as static geometry. PrepareOGF dispatches those objects to
// the skeleton exporter, whose Dynamic precondition does not hold in a scene.
inline bool PrepareOGSRPreviewGeometry(CEditableObject& object, IWriter& writer)
{
    // OGSR generates the tangent basis when uploading this preview vertex format.
    return object.PrepareRigidOGF(writer, false, nullptr);
}
