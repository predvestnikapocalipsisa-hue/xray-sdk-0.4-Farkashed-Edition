#include "stdafx.h"

void CRenderTarget::ProcessSMAA(CBackend& cmd_list)
{
    PIX_EVENT(SMAA);

    RenderScreenTriangle(cmd_list, rt_smaa_edgetex, s_pp_antialiasing->E[2]);
    RenderScreenTriangle(cmd_list, rt_smaa_blendtex, s_pp_antialiasing->E[3]);
    RenderScreenTriangle(cmd_list, rt_Generic_combine, s_pp_antialiasing->E[4]);

    HW.get_context(cmd_list.context_id)->CopyResource(rt_Generic_0->pSurface, rt_Generic_combine->pSurface);
}

void CRenderTarget::ProcessTAA(CBackend& cmd_list)
{
    PIX_EVENT(TAA);

    RenderScreenTriangle(cmd_list, rt_Generic_combine, s_taa->E[0]);
    HW.get_context(cmd_list.context_id)->CopyResource(rt_Generic_0->pSurface, rt_Generic_combine->pSurface);

    HW.get_context(cmd_list.context_id)->CopyResource(rt_Generic_0_prev->pSurface, rt_Generic_combine->pSurface);
}

//*****************************************************************************************************
#include <..\NVIDIA_DLSS\DLSS\include\nvsdk_ngx.h>
#include <..\NVIDIA_DLSS\DLSS\include\nvsdk_ngx_helpers.h>

#ifdef _DEBUG
#if _ITERATOR_DEBUG_LEVEL == 0
#pragma comment(lib, "nvsdk_ngx_s_dbg_iterator0")
#else
#pragma comment(lib, "nvsdk_ngx_s_dbg")
#endif
#else
#ifdef _DLL
#pragma comment(lib, "nvsdk_ngx_d")
#else
#pragma comment(lib, "nvsdk_ngx_s")
#endif
#endif

static class NGXWrapper
{
    NVSDK_NGX_Parameter* NgxParameters{};
    NVSDK_NGX_Handle* Handle{};
    bool DLSSCreated{}, DLSSInited{};
    ID3D11Resource* OutputRT{};
    NVSDK_NGX_Dimensions saved_renderSize{};

public:
    u32 saved_w{}, saved_h{};
    uint32_t dlssPreset{}, dlssQuality{};

    bool Create(const u64 appid, const NVSDK_NGX_Dimensions& renderSize, const NVSDK_NGX_Dimensions& displaySize, ref_rt& out_rt, const u32 quality, u32& preset)
    {
        OutputRT = out_rt->pSurface;
        saved_w = out_rt->dwWidth;
        saved_h = out_rt->dwHeight;
        saved_renderSize = renderSize;

        if (DLSSCreated)
        {
            Destroy();
        }

        if (HW.FeatureLevel < D3D_FEATURE_LEVEL_11_1)
        {
            Msg("!![%s] Low FeatureLevel: [%d]", __FUNCTION__, HW.FeatureLevel);
            // return false;
        }

        NVSDK_NGX_Result result{};
        if (!DLSSInited)
        {
            result = NVSDK_NGX_D3D11_Init(appid, L"", HW.pDevice);
            if (result != NVSDK_NGX_Result_Success)
            {
                Msg("!![%s] failed NVSDK_NGX_D3D11_Init. result: [%d]", __FUNCTION__, result);
                return false;
            }

            DLSSInited = true;
        }

        result = NVSDK_NGX_D3D11_GetCapabilityParameters(&NgxParameters);
        if (result != NVSDK_NGX_Result_Success)
        {
            Msg("!![%s] failed NVSDK_NGX_D3D11_GetCapabilityParameters. result: [%d]", __FUNCTION__, result);
            return false;
        }

        uint32_t needsUpdatedDriver{1};
        result = NgxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdatedDriver);
        if (needsUpdatedDriver)
        {
            Msg("!![%s] PLEASE UPDATE YOUR DRIVER", __FUNCTION__);
        }

        uint32_t dlssAvailable{};
        result = NgxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
        if (!dlssAvailable)
        {
            Msg("!![%s] DLSS NOT AVAILABLE", __FUNCTION__);
            NVSDK_NGX_D3D11_DestroyParameters(NgxParameters);
            return false;
        }

        const char* preset_name{};
        switch (quality)
        {
        case NVSDK_NGX_PerfQuality_Value_MaxPerf: preset_name = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance; break;
        case NVSDK_NGX_PerfQuality_Value_Balanced: preset_name = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced; break;
        case NVSDK_NGX_PerfQuality_Value_MaxQuality: preset_name = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality; break;
        case NVSDK_NGX_PerfQuality_Value_UltraPerformance: preset_name = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance; break;
        case NVSDK_NGX_PerfQuality_Value_UltraQuality: preset_name = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality; break;
        case NVSDK_NGX_PerfQuality_Value_DLAA: preset_name = NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA; break;
        }

        NVSDK_NGX_Parameter_SetUI(NgxParameters, preset_name, preset);

        result = NgxParameters->Get(preset_name, &dlssPreset);
        if (dlssPreset != preset)
        {
            Msg("!![%s] cannot change [%s] preset to: [%u], current: [%u]", __FUNCTION__, preset_name, preset, dlssPreset);
            preset = dlssPreset;
        }
        else
        {
            Msg("--[%s] [%s] current preset: [%u]", __FUNCTION__, preset_name, dlssPreset);
        }

        int32_t flags{};
        // Указывает, что вектор движения (Motion Vectors) представлен в низком разрешении. Это может снизить вычислительные затраты, но потенциально может снизить качество
        // итогового изображения. Когда вектор движения представлен в низком разрешении, это означает, что движение объектов отслеживается с меньшей детализацией. Например, вместо
        // того, чтобы иметь векторы движения для каждого пикселя, они могут быть рассчитаны для блоков пикселей (например, 4x4 или 8x8 пикселей).
        flags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

        // Указывает, что сцена рендерится в HDR (High Dynamic Range). Это позволяет DLSS корректно работать с HDR-контентом, учитывая более широкий диапазон яркости и
        // контрастности. https://learn.microsoft.com/ru-ru/windows/win32/direct3darticles/high-dynamic-range
        flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

        // Указывает на необходимость применения пост-обработки резкости к изображению после работы DLSS. Это улучшает четкость изображения, делая его визуально более
        // привлекательным. Использование флага NVSDK_NGX_DLSS_Feature_Flags_DoSharpening при инициализации и обновлении параметров DLSS является обязательным для активации функции
        // регулирования резкости через параметр InSharpness. Без этого флага значение sharpness не будет учитываться, и постобработка резкости не будет применена.
        /// flags |= NVSDK_NGX_DLSS_Feature_Flags_DoSharpening;

        // Указывает, что вектор движения подвергался дрожанию (Jittering). Это может использоваться для улучшения антиалиасинга путем случайного сдвига пикселей.
        // Флаг NVSDK_NGX_DLSS_Feature_Flags_MVJittered следует использовать, когда ваше приложение применяет джиттер в процессе рендеринга и вектора движения рассчитываются с
        // учетом этих смещений.
        /// flags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

        // Указывает, что нужно обрабатывать альфа-канал при масштабировании. Это важно для корректной работы с прозрачными объектами и элементами в сцене.
        // Этот флаг позволяет DLSS корректно обрабатывать прозрачные объекты, такие как дым, стекло, листву, волосы и другие элементы, которые имеют полупрозрачные пиксели.
        /// flags |= NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling;

        NVSDK_NGX_DLSS_Create_Params dlssCreateParams{};
        dlssCreateParams.Feature.InWidth = renderSize.Width;
        dlssCreateParams.Feature.InHeight = renderSize.Height;
        // final resolution
        dlssCreateParams.Feature.InTargetWidth = displaySize.Width;
        dlssCreateParams.Feature.InTargetHeight = displaySize.Height;
        dlssCreateParams.Feature.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(quality);
        dlssQuality = dlssCreateParams.Feature.InPerfQualityValue;

        dlssCreateParams.InFeatureCreateFlags = flags;
        result = NGX_D3D11_CREATE_DLSS_EXT(HW.get_context(CHW::IMM_CTX_ID), &Handle, NgxParameters, &dlssCreateParams);
        if (result != NVSDK_NGX_Result_Success)
        {
            Msg("!![%s] failed NGX_D3D11_CREATE_DLSS_EXT. result: [%d]", __FUNCTION__, result);
            return false;
        }

        DLSSCreated = true;
        return true;
    }

    void Destroy()
    {
        if (Handle)
        {
            NVSDK_NGX_D3D11_ReleaseFeature(Handle);
            Handle = nullptr;
        }

        if (NgxParameters)
        {
            NVSDK_NGX_D3D11_DestroyParameters(NgxParameters);
            NgxParameters = nullptr;
        }

        if (DLSSInited)
        {
            NVSDK_NGX_D3D11_Shutdown1(nullptr);
            DLSSInited = false;
        }

        DLSSCreated = false;
    }

    bool Draw() const
    {
        if (!DLSSCreated)
        {
            Msg("! NGXWrapper not created!");
            return false;
        }

        NVSDK_NGX_D3D11_DLSS_Eval_Params dlssEvalParams{};

        // Исходный цветовой рендер-таргет, который будет обрабатываться DLSS. DXGI_FORMAT_R16G16B16A16_FLOAT
        dlssEvalParams.Feature.pInColor = RImplementation.Target->rt_Generic_0->pSurface;
        // Ресурс для конечного (обработанного) цвета. Должен соответствовать формату исходного рендер-таргета или быть подходящим для конечного вывода. Обычно это
        // DXGI_FORMAT_R16G16B16A16_FLOAT.
        dlssEvalParams.Feature.pInOutput = OutputRT;
        // Буфер глубины. Обычно используется DXGI_FORMAT_R32_FLOAT для высокой точности глубины или DXGI_FORMAT_D32_FLOAT для использования в качестве буфера глубины. У нас же
        // DXGI_FORMAT_D24_UNORM_S8_UINT
        dlssEvalParams.pInDepth = RImplementation.Target->rt_zbuffer->pSurface;
        // Ресурс, содержащий векторы движения. DXGI_FORMAT_R16G16_FLOAT
        dlssEvalParams.pInMotionVectors = RImplementation.Target->rt_Velocity->pSurface;

        dlssEvalParams.InRenderSubrectDimensions = saved_renderSize;

        dlssEvalParams.InJitterOffsetX = ps_r_taa_jitter_full.x;
        dlssEvalParams.InJitterOffsetY = ps_r_taa_jitter_full.y;

        dlssEvalParams.InMVScaleX = -static_cast<float>(saved_renderSize.Width) * 0.5f;
        dlssEvalParams.InMVScaleY = static_cast<float>(saved_renderSize.Height) * 0.5f;

        const NVSDK_NGX_Result result = NGX_D3D11_EVALUATE_DLSS_EXT(HW.get_context(CHW::IMM_CTX_ID), Handle, NgxParameters, &dlssEvalParams);
        if (result != NVSDK_NGX_Result_Success)
        {
            Msg("! NGX_D3D11_EVALUATE_DLSS_EXT not valid. result: [%d]", result);
            return false;
        }

        return true;
    }

    ~NGXWrapper() { Destroy(); }
} NGXWrapper;

static float saved_3dss_scale_factor{};
bool CRenderTarget::reset_3dss_rendertarget(const bool need_reset)
{
    if (!need_reset && saved_3dss_scale_factor == ps_r_dlss_3dss_scale_factor)
        return false;

    saved_3dss_scale_factor = ps_r_dlss_3dss_scale_factor;

    u32 saved_w{}, saved_h{};
    bool empty_rt = !rt_Generic_combine_scope;
    if (!empty_rt)
    {
        saved_w = rt_Generic_combine_scope->dwWidth;
        saved_h = rt_Generic_combine_scope->dwHeight;
        rt_Generic_combine_scope.destroy();
    }

    u32 w{}, h{};
    for (float i{ps_r_dlss_3dss_scale_factor}; i >= 1.f; i -= 0.1f)
    {
        w = static_cast<u32>(std::ceil(static_cast<float>(Device.dwWidth) * i));
        h = static_cast<u32>(std::ceil(static_cast<float>(Device.dwHeight) * i));
        if (w < D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION && h < D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            break;
    }
    constexpr Flags32 flg{CRT::CreateUAV};
    rt_Generic_combine_scope.create(r2_RT_generic_combine_scope, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, flg);

    Msg("--[%s] 3D Scope render size: [%u, %u]", __FUNCTION__, w, h);

    return empty_rt || saved_w != w || saved_h != h;
}

void CRenderTarget::InitDLSS()
{
    NGXWrapper.Destroy();

    const NVSDK_NGX_Dimensions RenderParams{Device.dwWidth, Device.dwHeight};
    if (!NGXWrapper.Create(20082024151405ull, RenderParams, RenderParams, rt_Generic_combine, NVSDK_NGX_PerfQuality_Value_DLAA, ps_r_dlss_preset))
    {
        if (ps_r_pp_aa_mode == DLSS)
            ps_r_pp_aa_mode = FSR3;
    }
}

void CRenderTarget::DestroyDLSS()
{
    NGXWrapper.Destroy();
}

bool CRenderTarget::ProcessDLSS()
{
    PIX_EVENT(DLSS);

    if (ps_r_dlss_preset != NGXWrapper.dlssPreset)
    {
        InitDLSS();
    }

    if (!NGXWrapper.Draw())
    {
        Msg("!![%s] FAILED DLSS DRAW!", __FUNCTION__);
        return false;
    }

    HW.get_context(CHW::IMM_CTX_ID)->CopyResource(rt_Generic_0->pSurface, rt_Generic_combine->pSurface);
    return true;
}

//*****************************************************************************************************

void CRenderTarget::ProcessCAS(CBackend& cmd_list)
{
    if (fis_zero(ps_r_cas))
        return;

    PIX_EVENT(CAS);

    const Fvector4 params{std::max(ps_r_cas, 0.01f), 0.f, 0.f, 0.f};
    RenderScreenTriangle(cmd_list, rt_Generic_combine, s_cas->E[0], [&]() { cmd_list.set_c("f_cas_intensity", params); });
    HW.get_context(cmd_list.context_id)->CopyResource(rt_Generic_0->pSurface, rt_Generic_combine->pSurface);
}

//*****************************************************************************************************
#include "FidelityFX/host/ffx_fsr3.h"
#include "FidelityFX/host/backends/dx11/ffx_dx11.h"

static class Fsr3Wrapper
{
    bool fsr_created{};
    FfxFsr3UpscalerContext m_UpscalerContext{};
    xr_vector<char> m_scratchBuffer;
    ID3D11Resource* OutputRT{};
    FfxDimensions2D saved_maxRenderSize{}, saved_displaySize{};

    ID3D11Texture2D* dilatedDepth{};
    ID3D11Texture2D* dilatedMotionVectors{};
    ID3D11Texture2D* reconstructedPrevNearestDepth{};
public:
    u32 saved_w{}, saved_h{};

    bool Create(const FfxDimensions2D& maxRenderSize, const FfxDimensions2D& displaySize, ref_rt& out_rt)
    {
        OutputRT = out_rt->pSurface;
        saved_w = out_rt->dwWidth;
        saved_h = out_rt->dwHeight;
        saved_maxRenderSize = maxRenderSize;
        saved_displaySize = displaySize;

        if (fsr_created)
        {
            Destroy();
        }

        if (HW.FeatureLevel < D3D_FEATURE_LEVEL_11_0)
        {
            Msg("!![%s] Low FeatureLevel: [%d]", __FUNCTION__, HW.FeatureLevel);
        }

        m_scratchBuffer.resize(ffxGetScratchMemorySizeDX11(FFX_FSR3UPSCALER_CONTEXT_COUNT));

        FfxInterface fsrInterface{};
        auto fsrDevice = ffxGetDeviceDX11(HW.pDevice);
        FfxErrorCode errorCode = ffxGetInterfaceDX11(&fsrInterface, fsrDevice, m_scratchBuffer.data(), m_scratchBuffer.size(), FFX_FSR3UPSCALER_CONTEXT_COUNT);
        if (errorCode != FFX_OK)
        {
            Msg("!!Failed ffxGetInterfaceDX11! Error: [%d]", errorCode);
            Destroy();
            return false;
        }

        FfxFsr3UpscalerContextDescription m_UpscalercontextDesc{};
        m_UpscalercontextDesc.backendInterface = fsrInterface;
        m_UpscalercontextDesc.maxRenderSize = maxRenderSize;
        m_UpscalercontextDesc.maxUpscaleSize = displaySize;

        errorCode = ffxFsr3UpscalerContextCreate(&m_UpscalerContext, &m_UpscalercontextDesc);
        if (errorCode != FFX_OK)
        {
            Msg("!!Failed ffxFsr3UpscalerContextCreate! Error: [%d]", errorCode);
            Destroy();
            return false;
        }

        auto CreateTexture = [&](const DXGI_FORMAT fmt, const bool need_rt, ID3D11Texture2D** out) {
            D3D11_TEXTURE2D_DESC Desc{};
            Desc.Width = maxRenderSize.width;
            Desc.Height = maxRenderSize.height;
            Desc.MipLevels = 1;
            Desc.ArraySize = 1;
            Desc.Format = fmt;
            Desc.SampleDesc.Count = 1;
            Desc.Usage = D3D11_USAGE_DEFAULT;
            Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (need_rt)
                Desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
            return HW.pDevice->CreateTexture2D(&Desc, nullptr, out);
        };

        R_CHK(CreateTexture(DXGI_FORMAT_R32_FLOAT, true, &dilatedDepth));
        R_CHK(CreateTexture(DXGI_FORMAT_R16G16_FLOAT, true, &dilatedMotionVectors));
        R_CHK(CreateTexture(DXGI_FORMAT_R32_UINT, false, &reconstructedPrevNearestDepth));

        fsr_created = true;
        return true;
    }

    void Destroy()
    {
        if (fsr_created)
        {
            ffxFsr3UpscalerContextDestroy(&m_UpscalerContext);
            fsr_created = false;
        }

        _RELEASE(dilatedDepth);
        _RELEASE(dilatedMotionVectors);
        _RELEASE(reconstructedPrevNearestDepth);
    }

    bool Draw()
    {
        if (!fsr_created)
        {
            Msg("! Fsr3Wrapper not created!");
            return false;
        }

        FfxFsr3UpscalerDispatchDescription dispatchParameters{};

        dispatchParameters.commandList = ffxGetCommandListDX11(HW.get_context(CHW::IMM_CTX_ID));

        dispatchParameters.color = ffxGetResourceDX11(RImplementation.Target->rt_Generic_0->pSurface, GetFfxResourceDescriptionDX11(RImplementation.Target->rt_Generic_0->pSurface), L"FSR3_InputColor");
        dispatchParameters.depth = ffxGetResourceDX11(RImplementation.Target->rt_zbuffer->pSurface, GetFfxResourceDescriptionDX11(RImplementation.Target->rt_zbuffer->pSurface), L"FSR3_InputDepth");

        dispatchParameters.motionVectors = ffxGetResourceDX11(RImplementation.Target->rt_Velocity->pSurface, GetFfxResourceDescriptionDX11(RImplementation.Target->rt_Velocity->pSurface), L"FSR3_InputMotionVectors");
        dispatchParameters.exposure = ffxGetResourceDX11(nullptr, {}, L"FSR3_InputExposure");

        dispatchParameters.reactive = ffxGetResourceDX11(nullptr, {}, L"FSR3_InputReactiveMap");
        dispatchParameters.transparencyAndComposition = ffxGetResourceDX11(nullptr, {}, L"FSR3_TransparencyAndCompositionMap");

	    dispatchParameters.dilatedDepth = ffxGetResourceDX11(dilatedDepth, GetFfxResourceDescriptionDX11(dilatedDepth), L"FSR3_dilatedDepth", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatchParameters.dilatedMotionVectors =
            ffxGetResourceDX11(dilatedMotionVectors, GetFfxResourceDescriptionDX11(dilatedMotionVectors), L"FSR3_DilatedMotion", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatchParameters.reconstructedPrevNearestDepth = ffxGetResourceDX11(reconstructedPrevNearestDepth, GetFfxResourceDescriptionDX11(reconstructedPrevNearestDepth),
                                                                               L"FSR3_reconstructedPrevNearestDepth", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

        dispatchParameters.output = ffxGetResourceDX11(OutputRT, GetFfxResourceDescriptionDX11(OutputRT), L"FSR3_OutputUpscaledColor", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

        dispatchParameters.jitterOffset.x = ps_r_taa_jitter_full.x;
        dispatchParameters.jitterOffset.y = ps_r_taa_jitter_full.y;

        dispatchParameters.motionVectorScale.x = -static_cast<float>(saved_maxRenderSize.width) * 0.5f;
        dispatchParameters.motionVectorScale.y = static_cast<float>(saved_maxRenderSize.height) * 0.5f;

        dispatchParameters.frameTimeDelta = std::max(1.0f + EPS_L, Device.fTimeDeltaRealMS); // The time elapsed since the last frame (expressed in milliseconds).

        dispatchParameters.preExposure = 1.0f;

        dispatchParameters.renderSize = saved_maxRenderSize;
        dispatchParameters.upscaleSize = saved_displaySize;

        dispatchParameters.cameraFar = g_pGamePersistent->Environment().CurrentEnv->far_plane;
        dispatchParameters.cameraNear = VIEWPORT_NEAR;

        dispatchParameters.cameraFovAngleVertical = deg2rad(Device.fFOV);

        dispatchParameters.viewSpaceToMetersFactor = 1.0f;

        const FfxErrorCode errorCode = ffxFsr3UpscalerContextDispatch(&m_UpscalerContext, &dispatchParameters);

        if (errorCode != FFX_OK)
        {
            Msg("! ffxFsr3UpscalerContextDispatch not valid. Error: [%d]", errorCode);
            return false;
        }

        return true;
    }

    ~Fsr3Wrapper() { Destroy(); }
} Fsr3Wrapper , Fsr3WrapperScope;

void CRenderTarget::InitFSR()
{
    Fsr3Wrapper.Destroy();
    Fsr3WrapperScope.Destroy();

    const FfxDimensions2D displaySize{Device.dwWidth, Device.dwHeight};

    if (!Fsr3Wrapper.Create(displaySize, displaySize, rt_Generic_combine))
    {
        if (ps_r_pp_aa_mode == FSR3)
            ps_r_pp_aa_mode = TAA;
    }
    else
    {
        reset_3dss_rendertarget();

        const FfxDimensions2D ScopeSize{rt_Generic_combine_scope->dwWidth, rt_Generic_combine_scope->dwHeight};
        R_ASSERT(Fsr3WrapperScope.Create(displaySize, ScopeSize, rt_Generic_combine_scope));
    }
}

void CRenderTarget::DestroyFSR()
{
    Fsr3Wrapper.Destroy();
    Fsr3WrapperScope.Destroy();
}

bool CRenderTarget::ProcessFSR() const
{
    PIX_EVENT(FSR);

    if (!Fsr3Wrapper.Draw())
    {
        Msg("!![%s] FAILED FSR DRAW!", __FUNCTION__);
        return false;
    }

    HW.get_context(CHW::IMM_CTX_ID)->CopyResource(rt_Generic_0->pSurface, rt_Generic_combine->pSurface);
    return true;
}

bool CRenderTarget::ProcessFSR_3DSS(const bool need_reset)
{
    PIX_EVENT(3D_SCOPE_FSR);

    if (need_reset)
    {
        InitFSR();
    }

    if (!Fsr3WrapperScope.Draw())
    {
        Msg("!![%s] FAILED 3D SCOPE FSR DRAW!", __FUNCTION__);
        return false;
    }

    return true;
}
//*****************************************************************************************************

void CRenderTarget::PhaseAA(CBackend& cmd_list)
{
    if (ps_pnv_mode > 1) // skip AA for heatvision
        return;

    switch (ps_r_pp_aa_mode)
    {
        case DLSS: {
            if (!ProcessDLSS())
                ps_r_pp_aa_mode = FSR3;
            break;
        }
        case FSR3: {
            u_setrt(cmd_list, get_width(cmd_list), get_height(cmd_list), nullptr, nullptr, nullptr, nullptr);
            RImplementation.rmNormal(cmd_list);

            if (!ProcessFSR())
                ps_r_pp_aa_mode = TAA;
            break;
        }
        case TAA: ProcessTAA(cmd_list); break;
        case SMAA: ProcessSMAA(cmd_list); break;
    }

    if (ps_r_pp_aa_mode != SMAA)
        ProcessCAS(cmd_list);
}

//*****************************************************************************************************

bool CRenderTarget::Phase3DSSUpscale(CBackend& cmd_list)
{
    if (ps_r_dlss_3dss_scale_factor <= 1.f)
        return false;

    // Проверки на сглаживание для того, что для апскейлинга нам нужен taa джиттер
    if (ps_r_pp_aa_mode != DLSS && ps_r_pp_aa_mode != FSR3 && ps_r_pp_aa_mode != TAA)
        return false;

    bool need_reset = reset_3dss_rendertarget();
    if (!need_reset)
        need_reset = (Fsr3WrapperScope.saved_w != rt_Generic_combine_scope->dwWidth || Fsr3WrapperScope.saved_h != rt_Generic_combine_scope->dwHeight);

    return ProcessFSR_3DSS(need_reset);
}

//*****************************************************************************************************
