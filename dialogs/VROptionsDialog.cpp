#include "stdafx.h"
#include "resource.h"
#include "VROptionsDialog.h"

#define GET_WINDOW_MODES		(WM_USER+100)

static constexpr int rgwindowsize[] = { 640, 720, 800, 912, 1024, 1152, 1280, 1360, 1366, 1400, 1440, 1600, 1680, 1920, 2048, 2560, 3440, 3840, 4096, 5120, 6400, 7680, 8192, 11520, 15360 };  // windowed resolutions for selection list

constexpr float AAfactors[] = { 0.5f, 0.75f, 1.0f, 1.25f, (float)(4.0/3.0), 1.5f, 1.75f, 2.0f }; // factor is applied to width and to height, so 2.0f increases pixel count by 4. Additional values can be added.
constexpr int AAfactorCount = 8;

constexpr int MSAASamplesOpts[] = { 1, 4, 6, 8 };
constexpr int MSAASampleCount = 4;

static bool oldScaleValue = false;
static float scaleRelative = 1.0f;
static float scaleAbsolute = 55.0f;

static size_t getBestMatchingAAfactorIndex(float f)
{
   float delta = fabsf(f - AAfactors[0]);
   size_t bestMatch = 0;
   for (size_t i = 1; i < AAfactorCount; ++i)
      if (fabsf(f - AAfactors[i]) < delta) {
         delta = fabsf(f - AAfactors[i]);
         bestMatch = i;
      }
   return bestMatch;
}

VROptionsDialog::VROptionsDialog() : CDialog(IDD_VR_OPTIONS)
{
}

void VROptionsDialog::AddToolTip(const char * const text, HWND parentHwnd, HWND toolTipHwnd, HWND controlHwnd)
{
   TOOLINFO toolInfo = { };
   toolInfo.cbSize = sizeof(toolInfo);
   toolInfo.hwnd = parentHwnd;
   toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
   toolInfo.uId = (UINT_PTR)controlHwnd;
   toolInfo.lpszText = (char*)text;
   SendMessage(toolTipHwnd, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
}

void VROptionsDialog::ResetVideoPreferences()
{
   char tmp[256];
   constexpr float nudgeStrength = 2e-2f;
   sprintf_s(tmp, sizeof(tmp), "%f", nudgeStrength);
   SetDlgItemText(IDC_NUDGE_STRENGTH, tmp);

   const bool reflection = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "BallReflection"s, false);
   SendMessage(GetDlgItem(IDC_GLOBAL_REFLECTION_CHECK).GetHwnd(), BM_SETCHECK, reflection ? BST_CHECKED : BST_UNCHECKED, 0);

   SendMessage(GetDlgItem(IDC_SSSLIDER).GetHwnd(), TBM_SETPOS, TRUE, getBestMatchingAAfactorIndex(1.0f));
   SetDlgItemText(IDC_SSSLIDER_LABEL, "Supersampling Factor: 1.0");
   SendMessage(GetDlgItem(IDC_MSAASLIDER).GetHwnd(), TBM_SETPOS, TRUE, 1);
   SetDlgItemText(IDC_MSAASLIDER_LABEL, "MSAA Samples: Disabled");

   SendMessage(GetDlgItem(IDC_DYNAMIC_AO).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_ENABLE_AO).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_GLOBAL_SSREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_GLOBAL_PFREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);

   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_SETCURSEL, 0, 0);
   SendMessage(GetDlgItem(IDC_SCALE_FX_DMD).GetHwnd(), BM_SETCHECK, false ? BST_CHECKED : BST_UNCHECKED, 0);

   constexpr float vrSlope = 6.5f;
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrSlope);
   SetDlgItemText(IDC_VR_SLOPE, tmp);

   constexpr float vrOrientation = 0.0f;
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrOrientation);
   SetDlgItemText(IDC_3D_VR_ORIENTATION, tmp);

   constexpr float vrX = 0.0f;
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrX);
   SetDlgItemText(IDC_VR_OFFSET_X, tmp);

   constexpr float vrY = 0.0f;
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrY);
   SetDlgItemText(IDC_VR_OFFSET_Y, tmp);

   constexpr float vrZ = 80.0f;
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrZ);
   SetDlgItemText(IDC_VR_OFFSET_Z, tmp);

   SendMessage(GetDlgItem(IDC_BLOOM_OFF).GetHwnd(), BM_SETCHECK, false ? BST_CHECKED : BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_SETCURSEL, 1, 0);

   SendMessage(GetDlgItem(IDC_DMD_SOURCE).GetHwnd(), CB_SETCURSEL, 1, 0);
   SendMessage(GetDlgItem(IDC_BG_SOURCE).GetHwnd(), CB_SETCURSEL, 1, 0);

   SendMessage(GetDlgItem(IDC_VR_PREVIEW).GetHwnd(), CB_SETCURSEL, 0, 0);

   constexpr bool scaleToFixedWidth = false;
   oldScaleValue = scaleToFixedWidth;
   SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_SETCHECK, scaleToFixedWidth ? BST_CHECKED : BST_UNCHECKED, 0);

   scaleRelative = 1.0f;
   scaleAbsolute = 55.0f;
   sprintf_s(tmp, sizeof(tmp), scaleToFixedWidth ? "%0.1f" : "%0.3f", scaleToFixedWidth ? scaleAbsolute : scaleRelative);
   SetDlgItemText(IDC_VR_SCALE, tmp);

   constexpr float vrNearPlane = 5.0f;
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrNearPlane);
   SetDlgItemText(IDC_NEAR_PLANE, tmp);

   constexpr float vrFarPlane = 5000.0f;
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrFarPlane);
   SetDlgItemText(IDC_FAR_PLANE, tmp);

   //AMD Debug
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_SETCURSEL, 1, 0);
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_SETCURSEL, 0, 0);
}

BOOL VROptionsDialog::OnInitDialog()
{
   const HWND hwndDlg = GetHwnd();
   const HWND toolTipHwnd = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, g_pvp->theInstance, NULL);
   if (toolTipHwnd)
   {
      SendMessage(toolTipHwnd, TTM_SETMAXTIPWIDTH, 0, 180);
      HWND controlHwnd = GetDlgItem(IDC_BLOOM_OFF).GetHwnd();
      AddToolTip("Forces the bloom filter to be always off. Only for very low-end graphics cards.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_TURN_VR_ON).GetHwnd();
      AddToolTip("Disable Autodetect if Visual Pinball does not start up.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_DMD_SOURCE).GetHwnd();
      AddToolTip("What sources should be used for DMD?\nOnly internal supplied by script/Text Label/Flasher\nScreenreader (see screenreader.ini)\nFrom Shared Memory API", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_BG_SOURCE).GetHwnd();
      AddToolTip("What sources should be used for Backglass?\nOnly internal background\nTry to open a directb2s file\ndirectb2s file dialog\nScreenreader (see screenreader.ini)\nFrom Shared Memory API", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_NUDGE_STRENGTH).GetHwnd();
      AddToolTip("Changes the visual effect/screen shaking when nudging the table.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_DYNAMIC_AO).GetHwnd();
      AddToolTip("(Currently broken and disabled in VPVR)\r\n\r\nActivate this to enable dynamic Ambient Occlusion.\r\n\r\nThis slows down performance, but enables contact shadows for dynamic objects.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_ENABLE_AO).GetHwnd();
      AddToolTip("(Currently broken and disabled in VPVR)\r\n\r\nActivate this to enable Ambient Occlusion.\r\nThis enables contact shadows between objects.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_FXAACB).GetHwnd();
      AddToolTip("Enables post-processed Anti-Aliasing.\r\n\r\nThese settings can make the image quality a bit smoother at cost of performance and a slight blurring.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_VR_PREVIEW).GetHwnd();
      AddToolTip("Select which eye(s) to be displayed on the computer screen.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_SSSLIDER).GetHwnd();
      AddToolTip("Enables brute-force Up/Downsampling.\r\n\r\nThis delivers very good quality but has a significant impact on performance.\r\n\r\n2.0 means twice the resolution to be handled while rendering.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_MSAASLIDER).GetHwnd();
      AddToolTip("Set the amount of MSAA samples.\r\n\r\nMSAA can help reduce geometry aliasing in VR at the cost of performance and GPU memory.\r\n\r\nThis can really help improve image quality when not using supersampling.", hwndDlg, toolTipHwnd, controlHwnd);
      //AMD Debug
      controlHwnd = GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd();
      AddToolTip("Pixel format for VR Rendering.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_COMBO_BLIT).GetHwnd();
      AddToolTip("Blitting technique for VR Rendering.", hwndDlg, toolTipHwnd, controlHwnd);
   }

   char tmp[256];

   const float nudgeStrength = LoadValueFloatWithDefault(regKey[RegName::PlayerVR], "NudgeStrength"s, LoadValueFloatWithDefault(regKey[RegName::Player], "NudgeStrength"s, 2e-2f));
   sprintf_s(tmp, sizeof(tmp), "%f", nudgeStrength);
   SetDlgItemText(IDC_NUDGE_STRENGTH, tmp);

   const float AAfactor = LoadValueFloatWithDefault(regKey[RegName::PlayerVR], "AAFactor", LoadValueBoolWithDefault(regKey[RegName::Player], "USEAA", false) ? 1.5f : 1.0f);
   const HWND hwndSSSlider = GetDlgItem(IDC_SSSLIDER).GetHwnd();
   SendMessage(hwndSSSlider, TBM_SETRANGE, fTrue, MAKELONG(0, AAfactorCount - 1));
   SendMessage(hwndSSSlider, TBM_SETTICFREQ, 1, 0);
   SendMessage(hwndSSSlider, TBM_SETLINESIZE, 0, 1);
   SendMessage(hwndSSSlider, TBM_SETPAGESIZE, 0, 1);
   SendMessage(hwndSSSlider, TBM_SETTHUMBLENGTH, 5, 0);
   SendMessage(hwndSSSlider, TBM_SETPOS, TRUE, getBestMatchingAAfactorIndex(AAfactor));
   char SSText[32];
   sprintf_s(SSText, sizeof(SSText), "Supersampling Factor: %.2f", AAfactor);
   SetDlgItemText(IDC_SSSLIDER_LABEL, SSText);

   const int MSAASamples = LoadValueIntWithDefault(regKey[RegName::PlayerVR], "MSAASamples"s, 1);
   const auto CurrMSAAPos = std::find(MSAASamplesOpts, MSAASamplesOpts + (sizeof(MSAASamplesOpts) / sizeof(MSAASamplesOpts[0])), MSAASamples);
   const HWND hwndMSAASlider = GetDlgItem(IDC_MSAASLIDER).GetHwnd();
   SendMessage(hwndMSAASlider, TBM_SETRANGE, fTrue, MAKELONG(0, MSAASampleCount - 1));
   SendMessage(hwndMSAASlider, TBM_SETTICFREQ, 1, 0);
   SendMessage(hwndMSAASlider, TBM_SETLINESIZE, 0, 1);
   SendMessage(hwndMSAASlider, TBM_SETPAGESIZE, 0, 1);
   SendMessage(hwndMSAASlider, TBM_SETTHUMBLENGTH, 5, 0);
   SendMessage(hwndMSAASlider, TBM_SETPOS, TRUE, (LPARAM)std::distance(MSAASamplesOpts, CurrMSAAPos));
   char MSAAText[52];
   if (MSAASamples == 1)
   {
      sprintf_s(MSAAText, sizeof(MSAAText), "MSAA Samples: Disabled");
   }
   else
   {
      sprintf_s(MSAAText, sizeof(MSAAText), "MSAA Samples: %d", MSAASamples);
   }
   SetDlgItemText(IDC_MSAASLIDER_LABEL, MSAAText);

   bool useAO = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "DynamicAO"s, LoadValueBoolWithDefault(regKey[RegName::Player], "DynamicAO", false));
   SendMessage(GetDlgItem(IDC_DYNAMIC_AO).GetHwnd(), BM_SETCHECK, useAO ? BST_CHECKED : BST_UNCHECKED, 0);

   useAO = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "DisableAO"s, LoadValueBoolWithDefault(regKey[RegName::Player], "DisableAO"s, false));
   SendMessage(GetDlgItem(IDC_ENABLE_AO).GetHwnd(), BM_SETCHECK, useAO ? BST_UNCHECKED : BST_CHECKED, 0); // inverted logic

   const bool ssreflection = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "SSRefl"s, LoadValueBoolWithDefault(regKey[RegName::Player], "SSRefl"s, false));
   SendMessage(GetDlgItem(IDC_GLOBAL_SSREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, ssreflection ? BST_CHECKED : BST_UNCHECKED, 0);

   const bool pfreflection = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "PFRefl"s, LoadValueBoolWithDefault(regKey[RegName::Player], "PFRefl"s, true));
   SendMessage(GetDlgItem(IDC_GLOBAL_PFREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, pfreflection ? BST_CHECKED : BST_UNCHECKED, 0);

   const int fxaa = LoadValueIntWithDefault(regKey[RegName::PlayerVR], "FXAA"s, LoadValueIntWithDefault(regKey[RegName::Player], "FXAA"s, 0));
   HWND hwnd = GetDlgItem(IDC_FXAACB).GetHwnd();
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)"Disabled");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)"Fast FXAA");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)"Standard FXAA");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)"Quality FXAA");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)"Fast NFAA");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)"Standard DLAA");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)"Quality SMAA");
   SendMessage(hwnd, CB_SETCURSEL, fxaa, 0);

   const bool scaleFX_DMD = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "ScaleFXDMD"s, LoadValueBoolWithDefault(regKey[RegName::Player], "ScaleFXDMD"s, false));
   SendMessage(GetDlgItem(IDC_SCALE_FX_DMD).GetHwnd(), BM_SETCHECK, scaleFX_DMD ? BST_CHECKED : BST_UNCHECKED, 0);

   const VRPreviewMode vrPreview = (VRPreviewMode)LoadValueIntWithDefault(regKey[RegName::PlayerVR], "VRPreview"s, VRPREVIEW_LEFT);
   hwnd = GetDlgItem(IDC_VR_PREVIEW).GetHwnd();
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM) "Disabled");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM) "Left Eye");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM) "Right Eye");
   SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM) "Both Eyes");
   SendMessage(hwnd, CB_SETCURSEL, (int)vrPreview, 0);

   const bool scaleToFixedWidth = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "scaleToFixedWidth"s, false);
   oldScaleValue = scaleToFixedWidth;
   SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_SETCHECK, scaleToFixedWidth ? BST_CHECKED : BST_UNCHECKED, 0);

   scaleRelative = LoadValueFloatWithDefault(regKey[RegName::PlayerVR], "scaleRelative"s, 1.0f);
   scaleAbsolute = LoadValueFloatWithDefault(regKey[RegName::PlayerVR], "scaleAbsolute"s, 55.0f);

   sprintf_s(tmp, sizeof(tmp), scaleToFixedWidth ? "%0.1f" : "%0.3f", scaleToFixedWidth ? scaleAbsolute : scaleRelative);
   SetDlgItemText(IDC_VR_SCALE, tmp);

   const float vrNearPlane = LoadValueFloatWithDefault(regKey[RegName::PlayerVR], "nearPlane"s, 5.0f);
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrNearPlane);
   SetDlgItemText(IDC_NEAR_PLANE, tmp);

   const float vrFarPlane = LoadValueFloatWithDefault(regKey[RegName::PlayerVR], "farPlane"s, 5000.0f);
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrFarPlane);
   SetDlgItemText(IDC_FAR_PLANE, tmp);

   const float vrSlope = LoadValueFloatWithDefault(regKey[RegName::Player], "VRSlope"s, 6.5f);
   sprintf_s(tmp, sizeof(tmp), "%0.2f", vrSlope);
   SetDlgItemText(IDC_VR_SLOPE, tmp);

   const float vrOrientation = LoadValueFloatWithDefault(regKey[RegName::Player], "VROrientation"s, 0.0f);
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrOrientation);
   SetDlgItemText(IDC_3D_VR_ORIENTATION, tmp);

   const float vrX = LoadValueFloatWithDefault(regKey[RegName::Player], "VRTableX"s, 0.0f);
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrX);
   SetDlgItemText(IDC_VR_OFFSET_X, tmp);

   const float vrY = LoadValueFloatWithDefault(regKey[RegName::Player], "VRTableY"s, 0.0f);
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrY);
   SetDlgItemText(IDC_VR_OFFSET_Y, tmp);

   const float vrZ = LoadValueFloatWithDefault(regKey[RegName::Player], "VRTableZ"s, 80.0f);
   sprintf_s(tmp, sizeof(tmp), "%0.1f", vrZ);
   SetDlgItemText(IDC_VR_OFFSET_Z, tmp);

   const bool bloomOff = LoadValueBoolWithDefault(regKey[RegName::PlayerVR], "ForceBloomOff"s, LoadValueBoolWithDefault(regKey[RegName::Player], "ForceBloomOff"s, false));
   SendMessage(GetDlgItem(IDC_BLOOM_OFF).GetHwnd(), BM_SETCHECK, bloomOff ? BST_CHECKED : BST_UNCHECKED, 0);

   const int askToTurnOn = LoadValueIntWithDefault(regKey[RegName::PlayerVR], "AskToTurnOn"s, 1);
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"VR enabled");
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"VR autodetect");
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"VR disabled");
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_SETCURSEL, askToTurnOn, 0);

   const int DMDsource = LoadValueIntWithDefault(regKey[RegName::PlayerVR], "DMDsource"s, 1);
   SendMessage(GetDlgItem(IDC_DMD_SOURCE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Internal Text/Flasher (via vbscript)");
   SendMessage(GetDlgItem(IDC_DMD_SOURCE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Screenreader");
   SendMessage(GetDlgItem(IDC_DMD_SOURCE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"SharedMemory API");
   SendMessage(GetDlgItem(IDC_DMD_SOURCE).GetHwnd(), CB_SETCURSEL, DMDsource, 0);

   const int BGsource = LoadValueIntWithDefault(regKey[RegName::PlayerVR], "BGsource"s, 1);
   SendMessage(GetDlgItem(IDC_BG_SOURCE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Default table background");
   SendMessage(GetDlgItem(IDC_BG_SOURCE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"directb2s File (auto only)");
   SendMessage(GetDlgItem(IDC_BG_SOURCE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"directb2s File");
   SendMessage(GetDlgItem(IDC_BG_SOURCE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"SharedMemory API");
   SendMessage(GetDlgItem(IDC_BG_SOURCE).GetHwnd(), CB_SETCURSEL, BGsource, 0);

   //AMD Debugging
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGB 8");
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGBA 8 (Recommended)");
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGB 16F");
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGBA 16F");
   const int textureModeVR = LoadValueIntWithDefault(regKey[RegName::Player], "textureModeVR"s, 1);
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_SETCURSEL, textureModeVR, 0);

   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Blit (Recommended)");
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"BlitNamed");
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Shader");
   const int blitModeVR = LoadValueIntWithDefault(regKey[RegName::Player], "blitModeVR"s, 0);
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_SETCURSEL, blitModeVR, 0);

   return TRUE;
}

INT_PTR VROptionsDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_HSCROLL:
      {
         if ((HWND)lParam == GetDlgItem(IDC_SSSLIDER).GetHwnd()) {
            const size_t posAAfactor = SendMessage(GetDlgItem(IDC_SSSLIDER).GetHwnd(), TBM_GETPOS, 0, 0);//Reading the value from wParam does not work reliable
            const float AAfactor = ((posAAfactor) < AAfactorCount) ? AAfactors[posAAfactor] : 1.0f;
            char newText[32];
            sprintf_s(newText, sizeof(newText), "Supersampling Factor: %.2f", AAfactor);
            SetDlgItemText(IDC_SSSLIDER_LABEL, newText);
         }
         else if ((HWND)lParam == GetDlgItem(IDC_MSAASLIDER).GetHwnd()) {
            const size_t posMSAA = SendMessage(GetDlgItem(IDC_MSAASLIDER).GetHwnd(), TBM_GETPOS, 0, 0);//Reading the value from wParam does not work reliable
            const int MSAASampleAmount = MSAASamplesOpts[posMSAA];
            char newText[52];
            if (MSAASampleAmount == 1)
            {
               sprintf_s(newText, sizeof(newText), "MSAA Samples: Disabled");
            }
            else
            {
               sprintf_s(newText, sizeof(newText), "MSAA Samples: %d", MSAASampleAmount);
            }
            SetDlgItemText(IDC_MSAASLIDER_LABEL, newText);
         }
         break;
      }
   }

   return DialogProcDefault(uMsg, wParam, lParam);
}

BOOL VROptionsDialog::OnCommand(WPARAM wParam, LPARAM lParam)
{
   UNREFERENCED_PARAMETER(lParam);

   switch (LOWORD(wParam))
   {
      case IDC_DEFAULTS:
      {
         ResetVideoPreferences();
         break;
      }
      case IDC_SCALE_TO_CM:
      {
         const bool newScaleValue = SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_GETCHECK, 0, 0) > 0;
         if (oldScaleValue != newScaleValue) {
            CString tmpStr = GetDlgItemText(IDC_VR_SCALE);
            tmpStr.Replace(',', '.');
            if (oldScaleValue)
               scaleAbsolute = (float)atof(tmpStr.c_str());
            else
               scaleRelative = (float)atof(tmpStr.c_str());

            char tmp[256];
            sprintf_s(tmp, sizeof(tmp), newScaleValue ? "%0.1f" : "%0.3f", newScaleValue ? scaleAbsolute : scaleRelative);
            SetDlgItemText(IDC_VR_SCALE, tmp);
            oldScaleValue = newScaleValue;
         }
         break;
      }
      default:
         return FALSE;
   }
   return TRUE;
}

void VROptionsDialog::OnOK()
{
   SaveValue(regKey[RegName::PlayerVR], "NudgeStrength"s, GetDlgItemText(IDC_NUDGE_STRENGTH).c_str());

   const bool reflection = (SendMessage(GetDlgItem(IDC_GLOBAL_REFLECTION_CHECK).GetHwnd(), BM_GETCHECK, 0, 0) != 0);
   SaveValueBool(regKey[RegName::PlayerVR], "BallReflection"s, reflection);

   size_t fxaa = SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_GETCURSEL, 0, 0);
   if (fxaa == LB_ERR)
      fxaa = 0;
   SaveValueInt(regKey[RegName::PlayerVR], "FXAA"s, (int)fxaa);

   const bool scaleFX_DMD = SendMessage(GetDlgItem(IDC_SCALE_FX_DMD).GetHwnd(), BM_GETCHECK, 0, 0) != 0;
   SaveValueBool(regKey[RegName::PlayerVR], "ScaleFXDMD"s, scaleFX_DMD);

   const size_t AAfactorIndex = SendMessage(GetDlgItem(IDC_SSSLIDER).GetHwnd(), TBM_GETPOS, 0, 0);
   const float AAfactor = (AAfactorIndex < AAfactorCount) ? AAfactors[AAfactorIndex] : 1.0f;
   SaveValueFloat(regKey[RegName::PlayerVR], "AAFactor"s, AAfactor);

   const size_t MSAASamplesIndex = SendMessage(GetDlgItem(IDC_MSAASLIDER).GetHwnd(), TBM_GETPOS, 0, 0);
   const int MSAASamples = (MSAASamplesIndex < MSAASampleCount) ? MSAASamplesOpts[MSAASamplesIndex] : 1;
   SaveValueInt(regKey[RegName::PlayerVR], "MSAASamples"s, MSAASamples);

   bool useAO = SendMessage(GetDlgItem(IDC_DYNAMIC_AO).GetHwnd(), BM_GETCHECK, 0, 0) != 0;
   SaveValueBool(regKey[RegName::PlayerVR], "DynamicAO"s, useAO);

   useAO = SendMessage(GetDlgItem(IDC_ENABLE_AO).GetHwnd(), BM_GETCHECK, 0, 0) ? false : true; // inverted logic
   SaveValueBool(regKey[RegName::PlayerVR], "DisableAO"s, useAO);

   const bool ssreflection = SendMessage(GetDlgItem(IDC_GLOBAL_SSREFLECTION_CHECK).GetHwnd(), BM_GETCHECK, 0, 0) != 0;
   SaveValueBool(regKey[RegName::PlayerVR], "SSRefl"s, ssreflection);

   const bool pfreflection = SendMessage(GetDlgItem(IDC_GLOBAL_PFREFLECTION_CHECK).GetHwnd(), BM_GETCHECK, 0, 0) != 0;
   SaveValueBool(regKey[RegName::PlayerVR], "PFRefl"s, pfreflection);

   //AMD Debugging
   const size_t textureModeVR = SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt(regKey[RegName::Player], "textureModeVR"s, (int)textureModeVR);

   const size_t blitModeVR = SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt(regKey[RegName::Player], "blitModeVR"s, (int)blitModeVR);

   size_t vrPreview = SendMessage(GetDlgItem(IDC_VR_PREVIEW).GetHwnd(), CB_GETCURSEL, 0, 0);
   if (vrPreview == LB_ERR)
      vrPreview = VRPREVIEW_LEFT;
   SaveValueInt(regKey[RegName::PlayerVR], "VRPreview"s, (int)vrPreview);

   const bool scaleToFixedWidth = SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_GETCHECK, 0, 0) != 0;
   SaveValueBool(regKey[RegName::PlayerVR], "scaleToFixedWidth"s, scaleToFixedWidth);

   SaveValue(regKey[RegName::PlayerVR], scaleToFixedWidth ? "scaleAbsolute"s : "scaleRelative"s, GetDlgItemText(IDC_VR_SCALE).c_str());
   //SaveValueFloat(regKey[RegName::PlayerVR], scaleToFixedWidth ? "scaleRelative"s : "scaleAbsolute"s, scaleToFixedWidth ? scaleRelative : scaleAbsolute); //Also update hidden value?

   SaveValue(regKey[RegName::PlayerVR], "nearPlane"s, GetDlgItemText(IDC_NEAR_PLANE).c_str());

   SaveValue(regKey[RegName::PlayerVR], "farPlane"s, GetDlgItemText(IDC_FAR_PLANE).c_str());

   //For compatibility keep these in Player instead of PlayerVR
   SaveValue(regKey[RegName::Player], "VRSlope"s, GetDlgItemText(IDC_VR_SLOPE).c_str());

   SaveValue(regKey[RegName::Player], "VROrientation"s, GetDlgItemText(IDC_3D_VR_ORIENTATION).c_str());

   SaveValue(regKey[RegName::Player], "VRTableX"s, GetDlgItemText(IDC_VR_OFFSET_X).c_str());

   SaveValue(regKey[RegName::Player], "VRTableY"s, GetDlgItemText(IDC_VR_OFFSET_Y).c_str());

   SaveValue(regKey[RegName::Player], "VRTableZ"s, GetDlgItemText(IDC_VR_OFFSET_Z).c_str());

   const bool bloomOff = SendMessage(GetDlgItem(IDC_BLOOM_OFF).GetHwnd(), BM_GETCHECK, 0, 0) != 0;
   SaveValueBool(regKey[RegName::PlayerVR], "ForceBloomOff"s, bloomOff);

   const size_t askToTurnOn = SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt(regKey[RegName::PlayerVR], "AskToTurnOn"s, (int)askToTurnOn);

   const size_t dmdSource = SendMessage(GetDlgItem(IDC_DMD_SOURCE).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt(regKey[RegName::PlayerVR], "DMDsource"s, (int)dmdSource);

   const size_t bgSource = SendMessage(GetDlgItem(IDC_BG_SOURCE).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt(regKey[RegName::PlayerVR], "BGsource"s, (int)bgSource);

   CDialog::OnOK();
}

void VROptionsDialog::OnClose()
{
   CDialog::OnClose();
}
