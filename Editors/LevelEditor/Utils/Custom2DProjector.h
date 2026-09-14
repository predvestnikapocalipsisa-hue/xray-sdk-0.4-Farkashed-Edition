#pragma once

class CCustom2DProjector
{
protected:
    ref_shader shader_blended;
    ref_shader shader_overlap;
    u32 w;
    u32 h;
    U32Vec data;
    DEFINE_VECTOR(FVF::V, TVertVec, TVertIt);
    TVertVec mesh;
    ref_geom geom;

public:
    void OnImageChange(PropValue *prop);

public:
    shared_str name;

public:
    Fvector2 m_Offset;
    float m_Angle;

public:
    CCustom2DProjector();
    IC bool Valid() { return (w > 0) && (h > 0) && (!!data.size()); }
    IC void Clear()
    {
        name = "";
        w = 0;
        h = 0;
        geom = 0;
        data.clear();
        mesh.clear();
        m_Offset.set(0.f, 0.f);
        m_Angle = 0.f;
        DestroyShader();
    }
    bool LoadImage(LPCSTR nm);
    bool CreateNew(LPCSTR nm, u32 width, u32 height, u32 fill_color = 0);
    bool SaveImage();
    void ReloadImage();
    void CreateRMFromObjects(const Fbox &box, ObjectList &lst);
    void Render(bool blended);
    void CreateShader();
    void DestroyShader();
    IC LPCSTR GetName() { return *name; }
    IC u32 GetWidth() const { return w; }
    IC u32 GetHeight() const { return h; }
    IC U32Vec& GetData() { return data; }
    IC void SetSize(u32 width, u32 height) { w = width; h = height; }
    IC bool GetColor(u32 &color, int U, int V)
    {
        if (Valid() && (U >= 0) && (U < (int)w) && (V >= 0) && (V < (int)h))
        {
            color = data[V * w + U];
            return true;
        }
        return false;
    }
    IC void GetUV(float x, float z, float &u, float &v, const Fbox &box)
    {
        if (!Valid()) { u = 0.f; v = 0.f; return; }
        float bw = box.max.x - box.min.x;
        float bh = box.max.z - box.min.z;
        if (bw < EPS_L) bw = 1.0f;
        if (bh < EPS_L) bh = 1.0f;

        float cx = (box.min.x + box.max.x) * 0.5f;
        float cz = (box.min.z + box.max.z) * 0.5f;

        float dx = (x - cx) - m_Offset.x;
        float dz = (z - cz) - m_Offset.y;

        float rad = deg2rad(m_Angle);
        float cos_a = _cos(rad);
        float sin_a = _sin(rad);

        float rx = dx * cos_a - dz * sin_a;
        float rz = dx * sin_a + dz * cos_a;

        u = (rx / bw) + 0.5f;
        v = 0.5f - (rz / bh);
    }
    IC void GetPixelUV(float x, float z, int &U, int &V, const Fbox &box)
    {
        float u, v;
        GetUV(x, z, u, v, box);
        U = iFloor(u * (w - 1) + 0.5f);
        V = iFloor(v * (h - 1) + 0.5f);
    }
    IC float GetUFromX(float x, const Fbox &box)
    {
        float u, v;
        GetUV(x, 0.f, u, v, box);
        return u;
    }
    IC int GetPixelUFromX(float x, const Fbox &box)
    {
        int U, V;
        GetPixelUV(x, 0.f, U, V, box);
        if (U < 0) U = 0;
        return U;
    }
    IC float GetVFromZ(float z, const Fbox &box)
    {
        float u, v;
        GetUV(0.f, z, u, v, box);
        return v;
    }
    IC int GetPixelVFromZ(float z, const Fbox &box)
    {
        int U, V;
        GetPixelUV(0.f, z, U, V, box);
        if (V < 0) V = 0;
        return V;
    }
};
