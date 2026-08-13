#include "stdafx.h"

void ESceneAIMapTool::UnpackPosition(Fvector &Pdest, const NodePosition &Psrc, Fbox &bb, SAIParams &params)
{
    Pdest.x = float(Psrc.x) * params.fPatchSize;
    Pdest.y = (float(Psrc.y) / 65535) * (bb.max.y - bb.min.y) + bb.min.y;
    Pdest.z = float(Psrc.z) * params.fPatchSize;
}

u32 ESceneAIMapTool::UnpackLink(u32 &L)
{
    return L & 0x00ffffff;
}

void ESceneAIMapTool::PackPosition(NodePosition &Dest, Fvector &Src, Fbox &bb, SAIParams &params)
{
    float sp = 1 / params.fPatchSize;
    int px, py, pz;
    px = iFloor(Src.x * sp + EPS_L);
    py = iFloor(65535.f * (Src.y - bb.min.y) / (bb.max.y - bb.min.y) + EPS_L);
    pz = iFloor(Src.z * sp + EPS_L);

    clamp(px, -32767, 32767);
    Dest.x = s16(px);
    clamp(py, 0, 65535);
    Dest.y = u16(py);
    clamp(pz, -32767, 32767);
    Dest.z = s16(pz);
}

bool ESceneAIMapTool::Export(LPCSTR path)
{
    if (!Valid())
        return false;

    // Extended AI Map:
    // 0x0001 = обычный формат, ссылки 3 байта
    // 0x0002 = расширенный формат, ссылки 4 байта
    const bool extended = m_Flags.is(flExtendedAIMap);
    const u16 exportVersion =
        extended ? E_AIMAP_EXT_VERSION : E_AIMAP_VERSION;

    // calculate bbox
    Fbox bb;
    CalculateNodesBBox(bb);

    xr_string fn = xr_string(path) + "build.aimap";

    IWriter* F = FS.w_open(fn.c_str());

    if (!F)
    {
        ELog.Msg(
            mtError,
            "AIMap: Can't create build.aimap"
        );

        return false;
    }

    // --------------------------------------------------------
    // VERSION
    // --------------------------------------------------------

    F->open_chunk(E_AIMAP_CHUNK_VERSION);
    F->w_u16(exportVersion);
    F->close_chunk();

    // --------------------------------------------------------
    // BOX
    // --------------------------------------------------------

    F->open_chunk(E_AIMAP_CHUNK_BOX);
    F->w(&bb, sizeof(bb));
    F->close_chunk();

    // --------------------------------------------------------
    // PARAMS
    // --------------------------------------------------------

    F->open_chunk(E_AIMAP_CHUNK_PARAMS);
    F->w(&m_Params, sizeof(m_Params));
    F->close_chunk();

    // --------------------------------------------------------
    // NODES
    // --------------------------------------------------------

    EnumerateNodes();

    F->open_chunk(E_AIMAP_CHUNK_NODES);

    u32 nodesCount = (u32)m_Nodes.size();

    u32 defaultMaxNodesCount =
        (u32(1) << u32(MAX_NODE_BIT_COUNT)) - 2;

    if (nodesCount > defaultMaxNodesCount)
    {
        ELog.DlgMsg(
            mtInformation,
            "Warning. AI-Map contains %u nodes. "
            "Original compiler/game can handle only %u nodes. To compile this number of nodes, you need to enable Extended AI Map in the AI map parameters."
            "Extra: %u nodes",
            nodesCount,
            defaultMaxNodesCount,
            nodesCount - defaultMaxNodesCount
        );
    }

    F->w_u32(nodesCount);

    // --------------------------------------------------------
    // NODE DATA
    // --------------------------------------------------------

    for (AINodeIt it = m_Nodes.begin();
        it != m_Nodes.end();
        ++it)
    {
        u32 id;
        u16 pl;
        NodePosition np;

        // n1
        id = (*it)->n1
            ? (u32)(*it)->n1->idx
            : InvalidNode;

        F->w(&id, extended ? 4 : 3);

        // n2
        id = (*it)->n2
            ? (u32)(*it)->n2->idx
            : InvalidNode;

        F->w(&id, extended ? 4 : 3);

        // n3
        id = (*it)->n3
            ? (u32)(*it)->n3->idx
            : InvalidNode;

        F->w(&id, extended ? 4 : 3);

        // n4
        id = (*it)->n4
            ? (u32)(*it)->n4->idx
            : InvalidNode;

        F->w(&id, extended ? 4 : 3);

        // plane
        pl = pvCompress((*it)->Plane.n);
        F->w_u16(pl);

        // position
        PackPosition(
            np,
            (*it)->Pos,
            bb,
            m_Params
        );

        F->w(&np, sizeof(np));
    }

    F->close_chunk();

    FS.w_close(F);

    // --------------------------------------------------------
    // LOG
    // --------------------------------------------------------

    ELog.Msg(
        mtInformation,
        "AIMap: build.aimap exported. "
        "Version: 0x%04X, Links: %s, Nodes: %u",
        exportVersion,
        extended ? "4 bytes" : "3 bytes",
        nodesCount
    );

    return true;
}
/*
    u32 			id;
    u16 			pl;
    NodePosition 	np;
    F.r				(&id,3); 			n1 = (SAINode*)tools->UnpackLink(id);
    F.r				(&id,3); 			n2 = (SAINode*)tools->UnpackLink(id);
    F.r				(&id,3); 			n3 = (SAINode*)tools->UnpackLink(id);
    F.r				(&id,3); 			n4 = (SAINode*)tools->UnpackLink(id);
    pl				= F.r_u16(); 		pvDecompress(Plane.n,pl);
    F.r				(&np,sizeof(np)); 	tools->UnpackPosition(Pos,np,tools->m_BBox,tools->m_Params);
    Plane.build		(Pos,Plane.n);
*/
