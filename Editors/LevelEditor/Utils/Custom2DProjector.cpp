#include "stdafx.h"

// SBase
#define MAX_BUF_SIZE 0xFFFF

CCustom2DProjector::CCustom2DProjector()
{
	name = "";
	shader_overlap = 0;
	shader_blended = 0;
	m_Offset.set(0.f, 0.f);
	m_Angle = 0.f;
	w = 0;
	h = 0;
}

bool CCustom2DProjector::LoadImage(LPCSTR nm)
{
    Msg("! CCustom2DProjector::LoadImage: start, name='%s'", nm); FlushLog();
	name = nm;
	ImageLib.LoadTextureData(*name, data, w, h);
    Msg("! CCustom2DProjector::LoadImage: end, valid=%d", Valid()); FlushLog();
	return Valid();
}

bool CCustom2DProjector::CreateNew(LPCSTR nm, u32 width, u32 height, u32 fill_color)
{
	Msg("! CCustom2DProjector::CreateNew: name='%s', %dx%d", nm, width, height); FlushLog();
	name = nm;
	w = width;
	h = height;
	data.assign(w * h, fill_color);

	return SaveImage();
}

bool CCustom2DProjector::SaveImage()
{
	if (!Valid()) return false;

	STextureParams tp;
	ZeroMemory(&tp, sizeof(tp));
	tp.width = w;
	tp.height = h;
	tp.fmt = STextureParams::tfDXT1;
	tp.type = STextureParams::ttImage;
	tp.mip_filter = STextureParams::kMIPFilterTriangle;
	tp.flags.zero();
	tp.flags.set(STextureParams::flGenerateMipMaps, TRUE);
	tp.flags.set(STextureParams::flDitherColor, TRUE);

	xr_string rel_name = EFS.ChangeFileExt(*name, "");
	string_path fn;
	FS.update_path(fn, "$game_textures$", EFS.ChangeFileExt(*name, ".dds").c_str());

	if (ImageLib.MakeGameTexture(fn, data.data(), tp))
	{
		ETextureThumbnail* THM = (ETextureThumbnail*)ImageLib.CreateThumbnail(rel_name.c_str(), ECustomThumbnail::ETTexture);
		if (THM)
		{
			THM->Save();
			xr_delete(THM);
		}
		if (EDevice.Resources)
			EDevice.Resources->Evict();
		CreateShader();
		return true;
	}
	return false;
}

void CCustom2DProjector::CreateRMFromObjects(const Fbox &box, ObjectList &lst)
{
	geom.destroy();
	mesh.clear();
	for (ObjectIt it = lst.begin(); it != lst.end(); it++)
	{
		CSceneObject *S = (CSceneObject *)(*it);
		CEditableObject *O = S->GetReference();
		VERIFY(O);

		Fmatrix T;
		S->GetFullTransformToWorld(T);
		mesh.reserve(mesh.size() + S->GetFaceCount() * 3);
		for (EditMeshIt m_it = O->FirstMesh(); m_it != O->LastMesh(); m_it++)
		{
			for (u32 f_id = 0; f_id != (*m_it)->GetFCount(); f_id++)
			{
				FVF::V v;
				for (int k = 0; k < 3; k++)
				{
					T.transform_tiny(v.p, (*m_it)->GetVertices()[(*m_it)->GetFaces()[f_id].pv[k].pindex]);
					GetUV(v.p.x, v.p.z, v.t.x, v.t.y, box);
					mesh.push_back(v);
				}
			}
		}
	}
	geom.create(FVF::F_V, RCache.Vertex.Buffer(), 0);
}

void CCustom2DProjector::Render(bool blended)
{
	if (!Valid())
		return;
	EDevice.RenderNearer(0.001f);
	RCache.set_xform_world(Fidentity);
	EDevice.SetShader(blended ? shader_blended : shader_overlap);
	div_t cnt = div(mesh.size(), MAX_BUF_SIZE);
	u32 vBase;
	_VertexStream *Stream = &RCache.Vertex;
	for (int k = 0; k < cnt.quot; k++)
	{
		FVF::V *pv = (FVF::V *)Stream->Lock(MAX_BUF_SIZE, geom->vb_stride, vBase);
		CopyMemory(pv, mesh.data() + k * MAX_BUF_SIZE, sizeof(FVF::V) * MAX_BUF_SIZE);
		Stream->Unlock(MAX_BUF_SIZE, geom->vb_stride);
		EDevice.DP(D3DPT_TRIANGLELIST, geom, vBase, MAX_BUF_SIZE / 3);
	}
	if (cnt.rem)
	{
		FVF::V *pv = (FVF::V *)Stream->Lock(cnt.rem, geom->vb_stride, vBase);
		CopyMemory(pv, mesh.data() + cnt.quot * MAX_BUF_SIZE, sizeof(FVF::V) * cnt.rem);
		Stream->Unlock(cnt.rem, geom->vb_stride);
		EDevice.DP(D3DPT_TRIANGLELIST, geom, vBase, cnt.rem / 3);
	}
	EDevice.ResetNearer();
}

void CCustom2DProjector::CreateShader()
{
    Msg("! CCustom2DProjector::CreateShader: start, name='%s'", *name); FlushLog();
	DestroyShader();
	if (Valid())
	{
		shader_blended.create("editor\\do_base", *name);
		shader_overlap.create("default", *name);
		geom.create(FVF::F_V, RCache.Vertex.Buffer(), 0);
	}
    Msg("! CCustom2DProjector::CreateShader: end"); FlushLog();
}

void CCustom2DProjector::DestroyShader()
{
	geom.destroy();
	shader_blended.destroy();
	shader_overlap.destroy();
}

void CCustom2DProjector::OnImageChange(PropValue* prop)
{
	Msg("! CCustom2DProjector::OnImageChange: start"); FlushLog();
	LoadImage(*name);
	// DestroyShader(); ← убираем, CreateShader сам вызывает его внутри
	CreateShader();
	Msg("! CCustom2DProjector::OnImageChange: end"); FlushLog();
}

void CCustom2DProjector::ReloadImage()
{
	LoadImage(*name);
}
