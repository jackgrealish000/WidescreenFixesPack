#include "stdafx.h"
#include <d3d9.h>
#include "dxsdk\d3dvtbl.h"

uintptr_t pBarsPtr;
uintptr_t pBarsJmp;

struct Screen
{
    int32_t Width;
    int32_t Height;
    float fWidth;
    float fHeight;
    float fFieldOfView;
    float fAspectRatio;
    float fCustomAspectRatioHor;
    float fCustomAspectRatioVer;
    int32_t Width43;
    float fWidth43;
    float fHudScale;
    float fHudOffset;
    float fCutOffArea;
    float fFMVScale;
} Screen;

void __declspec(naked) BarsHook()
{
    __asm pushad
    if (*(uint32_t*)((*(uintptr_t*)pBarsPtr) + 0xCC) == 3)
    {
        __asm popad
        __asm jmp pBarsJmp
    }
    else
    {
        __asm popad
        __asm retn 0x0C
    }
}

typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
CreateDevice_t RealD3D9CreateDevice = NULL;
typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitive_t)(LPDIRECT3DDEVICE9, D3DPRIMITIVETYPE, UINT, UINT);
DrawPrimitive_t RealDrawPrimitive = NULL;

HRESULT WINAPI DrawPrimitive(LPDIRECT3DDEVICE9 pDevice, D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
    //hides some untextured objects and objects with broken geometry, may also hide normal objects
    //367 - tall grass, has some broken vertices, but hiding it is overkill
    if (PrimitiveType == D3DPT_TRIANGLESTRIP && StartVertex == 0 && (PrimitiveCount == 749 || PrimitiveCount == 909 /*|| PrimitiveCount == 367*/))
        return D3D_OK;

    return RealDrawPrimitive(pDevice, PrimitiveType, StartVertex, PrimitiveCount);
}

HRESULT WINAPI CreateDevice(IDirect3D9* d3ddev, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface)
{
    HRESULT retval = RealD3D9CreateDevice(d3ddev, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    UINT_PTR* pVTable = (UINT_PTR*)(*((UINT_PTR*)*ppReturnedDeviceInterface));
    RealDrawPrimitive = (DrawPrimitive_t)pVTable[IDirect3DDevice9VTBL::DrawPrimitive];
    injector::WriteMemory(&pVTable[IDirect3DDevice9VTBL::DrawPrimitive], &DrawPrimitive, true);
    return retval;
}

void InitSettings()
{
    auto pattern = hook::pattern("75 66 8D 4C 24 04 51");
    injector::MakeNOP(pattern.get_first(0), 2, true); //0x40BD3F

    pattern = hook::pattern("75 39 83 7C 24 08 01 75 32 8B"); //0x40BD6C
    struct RegHook
    {
        void operator()(injector::reg_pack& regs)
        {
            regs.ecx = *(uint32_t*)(regs.esp + 0x118);
            regs.edx = (regs.esp + 0x0C);

            GetModuleFileNameA(NULL, (char*)regs.edx, MAX_PATH);
            *(strrchr((char*)regs.edx, '\\') + 1) = '\0';
        }
    }; injector::MakeInline<RegHook>(pattern.get_first(0), pattern.get_first(20));
}

void Init()
{
    CIniReader iniReader("");
    static bool bCustomAR;
    auto szForceAspectRatio = iniReader.ReadString("MAIN", "ForceAspectRatio", "auto");
    static bool bFullscreenFMVs = iniReader.ReadInteger("MAIN", "FullscreenFMVs", 1) != 0;
    static float fMouseSensitivityFactor = iniReader.ReadFloat("MAIN", "MouseSensitivityFactor", 0.0f);
    static float fFOVFactor = iniReader.ReadFloat("MAIN", "FOVFactor", 0.0f);
    static bool bHideUntexturedObjects = iniReader.ReadInteger("MISC", "HideUntexturedObjects", 1) != 1;

    if (strncmp(szForceAspectRatio.c_str(), "auto", 4) != 0)
    {
        Screen.fCustomAspectRatioHor = static_cast<float>(std::stoi(szForceAspectRatio.c_str()));
        Screen.fCustomAspectRatioVer = static_cast<float>(std::stoi(strchr(szForceAspectRatio.c_str(), ':') + 1));
        bCustomAR = true;
    }

    auto pattern = hook::pattern("89 86 BC 02 00 00 89 8E C0 02 00 00 89 96 C4 02 00 00"); //0x9F2161 
    struct ResHook
    {
        void operator()(injector::reg_pack& regs)
        {
            Screen.Width = regs.eax;
            Screen.Height = regs.ecx;

            *(uint32_t*)(regs.esi + 0x2BC) = Screen.Width;
            *(uint32_t*)(regs.esi + 0x2C0) = Screen.Height;
            *(uint32_t*)(regs.esi + 0x2C4) = regs.edx;

            Screen.fWidth = static_cast<float>(Screen.Width);
            Screen.fHeight = static_cast<float>(Screen.Height);
            Screen.fAspectRatio = Screen.fWidth / Screen.fHeight;
            Screen.Width43 = static_cast<uint32_t>(Screen.fHeight * (4.0f / 3.0f));
            Screen.fWidth43 = static_cast<float>(Screen.Width43);
            Screen.fHudOffset = (1.0f / (Screen.fHeight * (4.0f / 3.0f))) * ((Screen.fWidth - Screen.fHeight * (4.0f / 3.0f)) / 2.0f);
            Screen.fHudScale = Screen.fHeight / Screen.fWidth;
            Screen.fFieldOfView = 1.0f * (((4.0f / 3.0f)) / (Screen.fAspectRatio));
            Screen.fCutOffArea = 0.5f / Screen.fFieldOfView;
            Screen.fFMVScale = 1.0f / (((4.0f / 3.0f)) / (Screen.fAspectRatio));

            if (bCustomAR)
            {
                Screen.fAspectRatio = Screen.fCustomAspectRatioHor / Screen.fCustomAspectRatioVer;
                Screen.fHudScale = Screen.fCustomAspectRatioVer / Screen.fCustomAspectRatioHor;
                Screen.fFieldOfView = 1.0f * (((4.0f / 3.0f)) / (Screen.fAspectRatio));
                Screen.fCutOffArea = 0.5f / Screen.fFieldOfView;
            }

            if (fFOVFactor)
            {
                Screen.fFieldOfView /= fFOVFactor;
                Screen.fCutOffArea = 0.5f / Screen.fFieldOfView;
            }
        }
    }; injector::MakeInline<ResHook>(pattern.get_first(0), pattern.get_first(18));

    auto dword_temp = *hook::pattern("D9 04 8D ? ? ? ? D9 5C 24 18 EB 0A").count(1).get(0).get<uint32_t*>(3);
    pattern = hook::pattern(pattern_str(0xD9, 0x04, '?', to_bytes(dword_temp))); //0xA01C67
    for (size_t i = 0; i < pattern.size(); i++)
    {
        injector::MakeNOP(pattern.get(i).get<uint32_t>(0), 1, true);
        injector::WriteMemory<uint16_t>(pattern.get(i).get<uint32_t>(1), 0x05D9, true);
        injector::WriteMemory(pattern.get(i).get<uint32_t>(3), &Screen.fHudScale, true);
    }

    auto ptr = hook::get_pattern("D8 3D ? ? ? ? D9 5C 24 34 E8 ? ? ? ? D9", 2); //0xA01E6A
    injector::WriteMemory(ptr, &Screen.fFieldOfView, true);

    //objects disappear at screen edges with hor+, fixing that @0098B8CF
    ptr = hook::get_pattern("D8 0D ? ? ? ? D9 5C 24 10 8B 5C 24 10 53", 2); //0x98B8CF
    injector::WriteMemory(ptr, &Screen.fCutOffArea, true);

    //FMVs
    pattern = hook::pattern("89 50 18 8B 06 8B CE FF 50 14 8B 4F 08"); //0xA25984
    struct FMVHook
    {
        void operator()(injector::reg_pack& regs)
        {
            //hor
            *(float*)(regs.eax - 0x54) /= Screen.fFMVScale;
            *(float*)(regs.eax - 0x38) /= Screen.fFMVScale;

            *(float*)(regs.eax - 0x1C) /= Screen.fFMVScale;
            *(float*)(regs.eax + 0x00) /= Screen.fFMVScale;

            if (bFullscreenFMVs)
            {
                static const float fFMVSize = (640.0f / 386.0f) / (480.0f / 386.0f);
                //hor
                *(float*)(regs.eax - 0x54) *= fFMVSize;
                *(float*)(regs.eax - 0x38) *= fFMVSize;

                *(float*)(regs.eax - 0x1C) *= fFMVSize;
                *(float*)(regs.eax + 0x00) *= fFMVSize;

                //ver
                *(float*)(regs.eax - 0x50) *= fFMVSize;
                *(float*)(regs.eax - 0x18) *= fFMVSize;

                *(float*)(regs.eax - 0x34) *= fFMVSize;
                *(float*)(regs.eax + 0x04) *= fFMVSize;
            }

            *(uintptr_t*)(regs.eax + 0x18) = regs.edx;
            regs.eax = *(uintptr_t*)(regs.esi);
        }
    }; injector::MakeInline<FMVHook>(pattern.get_first(0));

    pattern = hook::pattern("68 ? ? ? ? E8 ? ? ? ? 50 55 56 E8 ? ? ? ? 0F B7 4F 06 0F B7 57 04"); //0x992C4C
    static auto unk_CC0E20 = *pattern.count(1).get(0).get<char*>(1);
    struct TextHook
    {
        void operator()(injector::reg_pack& regs)
        {
            if (strncmp(unk_CC0E20, "16/9", 4) == 0)
            {
                strncpy(unk_CC0E20, "Bars", 4);
            }
            else
            {
                if (strncmp(unk_CC0E20, "4/3", 3) == 0)
                {
                    strncpy(unk_CC0E20, "Std", 4);
                }
            }

            regs.ecx = *(uint16_t*)(regs.edi + 6);
            regs.edx = *(uint16_t*)(regs.edi + 4);
        }
    }; injector::MakeInline<TextHook>(pattern.get_first(18), pattern.get_first(18 + 8));

    pBarsPtr = *(uintptr_t*)hook::get_pattern("A1 ? ? ? ? 8B 88 00 08 00 00 81 C1 B4 68 06 00", 1); //0xCBFBE0
    auto off_AD5CE8 = *hook::pattern("89 46 1C C7 06 ? ? ? ? 8B C6 5E C3").count(2).get(1).get<uintptr_t*>(5);
    injector::WriteMemory(off_AD5CE8 + 5, BarsHook, true); //0xAD5CFC
    pBarsJmp = (uintptr_t)hook::get_pattern("8B 41 10 6A 00 50 B9 ? ? ? ? E8", 0); //0xA2A350

    if (fMouseSensitivityFactor)
    {
        pattern = hook::pattern("D9 85 ? ? ? ? D8 1D ? ? ? ? DF E0 F6 C4 41 75 1D D9 85 4C"); //0x45F048
        struct MouseSensHook
        {
            void operator()(injector::reg_pack& regs)
            {
                *(float*)(regs.ebp - 0x1B0) *= fMouseSensitivityFactor;
                *(float*)(regs.ebp - 0x1B4) *= fMouseSensitivityFactor;

                float temp = *(float*)(regs.ebp - 0x1B4);
                _asm fld     dword ptr[temp]
            }
        }; injector::MakeInline<MouseSensHook>(pattern.get_first(0), pattern.get_first(6));
    }

    static const std::string defaultCmd("/B /lang:01  /spg:50 /GDBShaders KKMaps.bf");
    pattern = hook::pattern("C7 45 ? ? ? ? ? 68 04 01 00 00 8D 85 D8 FB FF FF 50"); //0x401A10
    struct StartupHook
    {
        void operator()(injector::reg_pack& regs)
        {
            *(uint32_t*)(regs.ebp - 0x4) = 0;
            std::string_view lpCmdLine(*(char**)(regs.ebp + 0x10));
            if (lpCmdLine.empty())
                *(uint32_t*)(regs.ebp + 0x10) = (uint32_t)defaultCmd.data();
        }
    }; injector::MakeInline<StartupHook>(pattern.get_first(0), pattern.get_first(7));

    if (true) //windowed mode text fix
    {
        auto[ResX, ResY] = GetDesktopRes();
        HKEY phkResult;
        DWORD cbData, Type;
        BYTE Data[4];

        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Ubisoft\\KingKong\\{2C391F94-B8B9-4832-9C57-3AFC332CC037}\\Basic video", 0, KEY_READ | KEY_SET_VALUE, &phkResult))
            RegCreateKeyA(HKEY_CURRENT_USER, "Software\\Ubisoft\\KingKong\\{2C391F94-B8B9-4832-9C57-3AFC332CC037}\\Basic video", &phkResult);

        cbData = 4;
        if (!RegQueryValueExA(phkResult, "ResolutionWidth", 0, &Type, Data, &cbData) && Type == 4 && cbData == 4)
            ResX = *(int32_t*)Data;
        else
            RegSetValueExA(phkResult, "ResolutionWidth", 0, REG_DWORD, (const BYTE*)&ResX, cbData);
        cbData = 4;
        if (!RegQueryValueExA(phkResult, "ResolutionHeight", 0, &Type, Data, &cbData) && Type == 4 && cbData == 4)
            ResY = *(int32_t*)Data;
        else
            RegSetValueExA(phkResult, "ResolutionHeight", 0, REG_DWORD, (const BYTE*)&ResY, cbData);
        RegCloseKey(phkResult);

        pattern = hook::pattern("68 E0 01 00 00 68 80 02 00 00");
        for (size_t i = 0; i < pattern.size(); ++i)
        {
            injector::WriteMemory(pattern.get(i).get<uint32_t>(6), ResX, true);
            injector::WriteMemory(pattern.get(i).get<uint32_t>(1), ResY, true);
        }
    }

    if (bHideUntexturedObjects)
    {
        pattern = hook::pattern("8B 84 24 90 04 00 00 50 8B CD E8 ? ? ? ? 8B 45 00 8B 08 50");
        struct Direct3DDeviceHook
        {
            void operator()(injector::reg_pack& regs)
            {
                auto pID3D9 = *(IDirect3D9**)(regs.ebp + 0x00);
                UINT_PTR* pVTable = (UINT_PTR*)(*((UINT_PTR*)pID3D9));
                RealD3D9CreateDevice = (CreateDevice_t)pVTable[IDirect3D9VTBL::CreateDevice];
                injector::WriteMemory(&pVTable[IDirect3D9VTBL::CreateDevice], &CreateDevice, true);
                regs.eax = *(uint32_t*)(regs.esp + 0x490);
            }
        }; injector::MakeInline<Direct3DDeviceHook>(pattern.get_first(0), pattern.get_first(7));
    }
}

CEXP void InitializeASI()
{
    std::call_once(CallbackHandler::flag, []()
        {
            CallbackHandler::RegisterCallback(Init, hook::pattern("33 DB 89 5D E0 53"));
            CallbackHandler::RegisterCallback(InitSettings, hook::pattern("75 66 8D 4C 24 04 51"));
        });
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        if (!IsUALPresent()) { InitializeASI(); }
    }
    return TRUE;
}
