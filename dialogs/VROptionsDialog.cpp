#include "StdAfx.h"
#include "resource.h"
#include "VROptionsDialog.h"

#define GET_WINDOW_MODES		WM_USER+100
#define RESET_SIZELIST_CONTENT	WM_USER+102

const int rgwindowsize[] = { 640, 720, 800, 912, 1024, 1152, 1280, 1600 };  // windowed resolutions for selection list

const float AAfactors[] = { 0.5f, 0.75f, 1.0f, 1.25f, 4.0f / 3.0f, 1.5f, 1.75f, 2.0f }; // factor is applied to width and to height, so 2.0f increases pixel count by 4. Additional values can be added.
const int AAfactorCount = 8;

static bool oldScaleValue = false;
static float scaleRelative = 1.0f;
static float scaleAbsolute = 55.0f;

size_t VROptionsDialog::getBestMatchingAAfactorIndex(float f)
{
   float delta = fabs(f - AAfactors[0]);
   size_t bestMatch = 0;
   for (size_t i = 1; i < AAfactorCount; ++i)
      if (fabs(f - AAfactors[i]) < delta) {
         delta = fabs(f - AAfactors[i]);
         bestMatch = i;
      }
   return bestMatch;
}

VROptionsDialog::VROptionsDialog() : CDialog(IDD_VR_OPTIONS)
{
}

void VROptionsDialog::AddToolTip(char *text, HWND parentHwnd, HWND toolTipHwnd, HWND controlHwnd)
{
   TOOLINFO toolInfo = { 0 };
   toolInfo.cbSize = sizeof(toolInfo);
   toolInfo.hwnd = parentHwnd;
   toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
   toolInfo.uId = (UINT_PTR)controlHwnd;
   toolInfo.lpszText = text;
   SendMessage(toolTipHwnd, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
}

void VROptionsDialog::ResetVideoPreferences() // 0 = default, 1 = lowend PC, 2 = highend PC
{
   const int widthcur = LoadValueIntWithDefault("PlayerVR", "Width", DEFAULT_PLAYER_WIDTH);

   const int heightcur = LoadValueIntWithDefault("PlayerVR", "Height", widthcur * 9 / 16);

   SendMessage(GetHwnd(), GET_WINDOW_MODES, widthcur << 16, heightcur << 16 | 32);

   char tmp[256];
   float nudgeStrength = 2e-2f;
   sprintf_s(tmp, 256, "%f", nudgeStrength);
   SetDlgItemTextA(IDC_NUDGE_STRENGTH, tmp);

   SendMessage(GetDlgItem(IDC_SSSLIDER).GetHwnd(), TBM_SETPOS, TRUE, getBestMatchingAAfactorIndex(1.0f));
   SetDlgItemText(IDC_SSSLIDER_LABEL, "Supersampling Factor: 1.0");

   SendMessage(GetDlgItem(IDC_DYNAMIC_AO).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_ENABLE_AO).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_GLOBAL_SSREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_GLOBAL_PFREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);

   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_SETCURSEL, 0, 0);
   SendMessage(GetDlgItem(IDC_SCALE_FX_DMD).GetHwnd(), BM_SETCHECK, false ? BST_CHECKED : BST_UNCHECKED, 0);

   const float vrSlope = 6.5f;
   sprintf_s(tmp, 256, "%0.1f", vrSlope);
   SetDlgItemTextA(IDC_VR_SLOPE, tmp);
   
   const float vrOrientation = 0.0f;
   sprintf_s(tmp, 256, "%0.1f", vrOrientation);
   SetDlgItemTextA(IDC_3D_VR_ORIENTATION, tmp);
   
   const float vrX = 0.0f;
   sprintf_s(tmp, 256, "%0.1f", vrX);
   SetDlgItemTextA(IDC_VR_OFFSET_X, tmp);
   
   const float vrY = 0.0f;
   sprintf_s(tmp, 256, "%0.1f", vrY);
   SetDlgItemTextA(IDC_VR_OFFSET_Y, tmp);
   
   const float vrZ = 80.0f;
   sprintf_s(tmp, 256, "%0.1f", vrZ);
   SetDlgItemTextA(IDC_VR_OFFSET_Z, tmp);

   SendMessage(GetDlgItem(IDC_BLOOM_OFF).GetHwnd(), BM_SETCHECK, false ? BST_CHECKED : BST_UNCHECKED, 0);
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_SETCURSEL, 1, 0);
   SendMessage(GetDlgItem(IDC_DISPLAY_ID).GetHwnd(), CB_SETCURSEL, 0, 0);

   SendMessage(GetDlgItem(IDC_VR_DISABLE_PREVIEW).GetHwnd(), BM_SETCHECK, BST_UNCHECKED, 0);
   
   const bool scaleToFixedWidth = false;
   oldScaleValue = scaleToFixedWidth;
   SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_SETCHECK, scaleToFixedWidth ? BST_CHECKED : BST_UNCHECKED, 0);

   scaleRelative = 1.0f;
   scaleAbsolute = 55.0f;
   sprintf_s(tmp, 256, scaleToFixedWidth ? "%0.1f" : "%0.3f", scaleToFixedWidth ? scaleAbsolute : scaleRelative);
   SetDlgItemTextA(IDC_VR_SCALE, tmp);

   const float vrNearPlane = 5.0f;
   sprintf_s(tmp, 256, "%0.1f", vrNearPlane);
   SetDlgItemTextA(IDC_NEAR_PLANE, tmp);

   const float vrFarPlane = 500.0f;
   sprintf_s(tmp, 256, "%0.1f", vrFarPlane);
   SetDlgItemTextA(IDC_FAR_PLANE, tmp);

   //AMD Debug
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_SETCURSEL, 1, 0);
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_SETCURSEL, 0, 0);
}

void VROptionsDialog::FillVideoModesList(const std::vector<VideoMode>& modes, const VideoMode* curSelMode)
{
   const HWND hwndList = GetDlgItem(IDC_SIZELIST).GetHwnd();
   SendMessage(hwndList, LB_RESETCONTENT, 0, 0);
   int bestMatch = 0; // to find closest matching res
   int bestMatchingPoints = 0; // dto.

   int screenwidth;
   int screenheight;
   int x, y;
   const int display = (int)SendMessage(GetDlgItem(IDC_DISPLAY_ID).GetHwnd(), CB_GETCURSEL, 0, 0);
   getDisplaySetupByID(display, x, y, screenwidth, screenheight);

   for (size_t i = 0; i < modes.size(); ++i)
   {
      char szT[128];

#ifdef ENABLE_SDL
      if (modes[i].depth) // i.e. is this windowed or not
         sprintf_s(szT, "%d x %d (%dHz) %s", modes[i].width, modes[i].height, modes[i].refreshrate, (modes[i].depth == 32) ? "32bit" :
            (modes[i].depth == 30) ? "HDR" :
            (modes[i].depth == 16) ? "16bit" : "");
#else
      if (modes[i].depth)
         sprintf_s(szT, "%d x %d (%dHz)", modes[i].width, modes[i].height, /*modes[i].depth,*/ modes[i].refreshrate);
#endif
      else
         sprintf_s(szT, "%d x %d", modes[i].width, modes[i].height);

      SendMessage(hwndList, LB_ADDSTRING, 0, (LPARAM)szT);
      if (curSelMode) {
         int matchingPoints = 0;
         if (modes[i].width == curSelMode->width) matchingPoints += 100;
         if (modes[i].height == curSelMode->height) matchingPoints += 100;
         if (modes[i].depth == curSelMode->depth) matchingPoints += 50;
         if (modes[i].width == screenwidth) matchingPoints += 3;
         if (modes[i].height == screenheight) matchingPoints += 3;
         if (matchingPoints > bestMatchingPoints) {
            bestMatch = (int)i;
            bestMatchingPoints = matchingPoints;
         }
      }
   }
   SendMessage(hwndList, LB_SETCURSEL, bestMatch, 0);
}

BOOL VROptionsDialog::OnInitDialog()
{
   const HWND hwndDlg = GetHwnd();
   const HWND toolTipHwnd = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, NULL, g_hinst, NULL);
   if (toolTipHwnd)
   {
      SendMessage(toolTipHwnd, TTM_SETMAXTIPWIDTH, 0, 180);
      HWND controlHwnd = GetDlgItem(IDC_BLOOM_OFF).GetHwnd();
      AddToolTip("Forces the bloom filter to be always off. Only for very low-end graphics cards.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_TURN_VR_ON).GetHwnd();
      AddToolTip("Disable Autodetect if Visual Pinball does not start up.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_NUDGE_STRENGTH).GetHwnd();
      AddToolTip("Changes the visual effect/screen shaking when nudging the table.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_DYNAMIC_AO).GetHwnd();
      AddToolTip("Activate this to enable dynamic Ambient Occlusion.\r\nThis slows down performance, but enables contact shadows for dynamic objects.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_ENABLE_AO).GetHwnd();
      AddToolTip("Activate this to enable Ambient Occlusion.\r\nThis enables contact shadows between objects.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_FXAACB).GetHwnd();
      AddToolTip("Enables post-processed Anti-Aliasing.\r\nThis delivers smoother images, at the cost of slight blurring.\r\n'Quality FXAA' and 'Quality SMAA' are recommended and lead to less artifacts,\nbut will harm performance on low-end graphics cards.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_SSSLIDER).GetHwnd();
      AddToolTip("Enables brute-force Up/Downsampling.\r\nThis delivers very good quality, but slows down performance significantly.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_DISPLAY_ID).GetHwnd();
      AddToolTip("Select Display for Video output.", hwndDlg, toolTipHwnd, controlHwnd);
      //AMD Debug
      controlHwnd = GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd();
      AddToolTip("Pixel format for VR Rendering.", hwndDlg, toolTipHwnd, controlHwnd);
      controlHwnd = GetDlgItem(IDC_COMBO_BLIT).GetHwnd();
      AddToolTip("Blitting technique for VR Rendering.", hwndDlg, toolTipHwnd, controlHwnd);
   }

   char tmp[256];

   const float nudgeStrength = LoadValueFloatWithDefault("PlayerVR", "NudgeStrength", LoadValueFloatWithDefault("Player", "NudgeStrength", 2e-2f));
   sprintf_s(tmp, 256, "%f", nudgeStrength);
   SetDlgItemTextA(IDC_NUDGE_STRENGTH, tmp);

   const float AAfactor = LoadValueFloatWithDefault("PlayerVR", "AAFactor", LoadValueBoolWithDefault("Player", "USEAA", false) ? 1.5f : 1.0f);
   const HWND hwndSSSlider = GetDlgItem(IDC_SSSLIDER).GetHwnd();
   SendMessage(hwndSSSlider, TBM_SETRANGE, fTrue, MAKELONG(0, AAfactorCount - 1));
   SendMessage(hwndSSSlider, TBM_SETTICFREQ, 1, 0);
   SendMessage(hwndSSSlider, TBM_SETLINESIZE, 0, 1);
   SendMessage(hwndSSSlider, TBM_SETPAGESIZE, 0, 1);
   SendMessage(hwndSSSlider, TBM_SETTHUMBLENGTH, 5, 0);
   SendMessage(hwndSSSlider, TBM_SETPOS, TRUE, getBestMatchingAAfactorIndex(AAfactor));
   char newText[32];
   sprintf_s(newText, "Supersampling Factor: %.2f", AAfactor);
   SetDlgItemText(IDC_SSSLIDER_LABEL, newText);

   int useAO = LoadValueIntWithDefault("PlayerVR", "DynamicAO", LoadValueIntWithDefault("Player", "DynamicAO", 0));
   SendMessage(GetDlgItem(IDC_DYNAMIC_AO).GetHwnd(), BM_SETCHECK, (useAO != 0) ? BST_CHECKED : BST_UNCHECKED, 0);

   useAO = LoadValueIntWithDefault("PlayerVR", "DisableAO", LoadValueIntWithDefault("Player", "DisableAO", 0));
   SendMessage(GetDlgItem(IDC_ENABLE_AO).GetHwnd(), BM_SETCHECK, (useAO != 0) ? BST_UNCHECKED : BST_CHECKED, 0); // inverted logic

   const int ssreflection = LoadValueIntWithDefault("PlayerVR", "SSRefl", LoadValueIntWithDefault("Player", "SSRefl", 0));
   SendMessage(GetDlgItem(IDC_GLOBAL_SSREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, ssreflection ? BST_CHECKED : BST_UNCHECKED, 0);

   const int pfreflection = LoadValueIntWithDefault("PlayerVR", "PFRefl", LoadValueIntWithDefault("PlayerVR", "PFRefl", 1));
   SendMessage(GetDlgItem(IDC_GLOBAL_PFREFLECTION_CHECK).GetHwnd(), BM_SETCHECK, pfreflection ? BST_CHECKED : BST_UNCHECKED, 0);

   const int fxaa = LoadValueIntWithDefault("PlayerVR", "FXAA", LoadValueIntWithDefault("Player", "FXAA", 0));

   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Disabled");
   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Fast FXAA");
   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Standard FXAA");
   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Quality FXAA");
   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Fast NFAA");
   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Standard DLAA");
   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Quality SMAA");
   SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_SETCURSEL, fxaa, 0);

   const bool scaleFX_DMD = LoadValueBoolWithDefault("PlayerVR", "ScaleFXDMD", LoadValueBoolWithDefault("Player", "ScaleFXDMD", false));
   SendMessage(GetDlgItem(IDC_SCALE_FX_DMD).GetHwnd(), BM_SETCHECK, scaleFX_DMD ? BST_CHECKED : BST_UNCHECKED, 0);

   const bool disableVRPreview = LoadValueBoolWithDefault("PlayerVR", "VRPreviewDisabled", 0);
   SendMessage(GetDlgItem(IDC_VR_DISABLE_PREVIEW).GetHwnd(), BM_SETCHECK, disableVRPreview ? BST_CHECKED : BST_UNCHECKED, 0);

   const bool scaleToFixedWidth = LoadValueBoolWithDefault("PlayerVR", "scaleToFixedWidth", 0);
   oldScaleValue = scaleToFixedWidth;
   SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_SETCHECK, scaleToFixedWidth ? BST_CHECKED : BST_UNCHECKED, 0);

   scaleRelative = LoadValueFloatWithDefault("PlayerVR", "scaleRelative", 1.0f);
   scaleAbsolute = LoadValueFloatWithDefault("PlayerVR", "scaleAbsolute", 55.0f);

   sprintf_s(tmp, 256, scaleToFixedWidth ? "%0.1f" : "%0.3f", scaleToFixedWidth ? scaleAbsolute : scaleRelative);
   SetDlgItemTextA(IDC_VR_SCALE, tmp);

   const float vrNearPlane = LoadValueFloatWithDefault("PlayerVR", "nearPlane", 5.0f);
   sprintf_s(tmp, 256, "%0.1f", vrNearPlane);
   SetDlgItemTextA(IDC_NEAR_PLANE, tmp);

   const float vrFarPlane = LoadValueFloatWithDefault("PlayerVR", "farPlane", 500.0f);
   sprintf_s(tmp, 256, "%0.1f", vrFarPlane);
   SetDlgItemTextA(IDC_FAR_PLANE, tmp);

   const float vrSlope = LoadValueFloatWithDefault("Player", "VRSlope", 6.5f);
   sprintf_s(tmp, 256, "%0.2f", vrSlope);
   SetDlgItemTextA(IDC_VR_SLOPE, tmp);

   const float vrOrientation = LoadValueFloatWithDefault("Player", "VROrientation", 0.0f);
   sprintf_s(tmp, 256, "%0.1f", vrOrientation);
   SetDlgItemTextA(IDC_3D_VR_ORIENTATION, tmp);

   const float vrX = LoadValueFloatWithDefault("Player", "VRTableX", 0.0f);
   sprintf_s(tmp, 256, "%0.1f", vrX);
   SetDlgItemTextA(IDC_VR_OFFSET_X, tmp);

   const float vrY = LoadValueFloatWithDefault("Player", "VRTableY", 0.0f);
   sprintf_s(tmp, 256, "%0.1f", vrY);
   SetDlgItemTextA(IDC_VR_OFFSET_Y, tmp);

   const float vrZ = LoadValueFloatWithDefault("Player", "VRTableZ", 80.0f);
   sprintf_s(tmp, 256, "%0.1f", vrZ);
   SetDlgItemTextA(IDC_VR_OFFSET_Z, tmp);

   const int bloomOff = LoadValueIntWithDefault("PlayerVR", "ForceBloomOff", LoadValueIntWithDefault("Player", "ForceBloomOff", false));
   SendMessage(GetDlgItem(IDC_BLOOM_OFF).GetHwnd(), BM_SETCHECK, bloomOff ? BST_CHECKED : BST_UNCHECKED, 0);

   const int askToTurnOn = LoadValueIntWithDefault("PlayerVR", "AskToTurnOn", 1);
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"VR enabled");
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"VR autodetect");
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"VR disabled");
   SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_SETCURSEL, askToTurnOn, 0);

   int display;
   HRESULT hr = LoadValueInt("PlayerVR", "Display", &display);
   std::vector<DisplayConfig> displays;
   getDisplayList(displays);

   if ((hr != S_OK) || ((int)displays.size() <= display) || (display<-1))
      display = -1;

   SendMessage(GetDlgItem(IDC_DISPLAY_ID).GetHwnd(), CB_RESETCONTENT, 0, 0);

   for (std::vector<DisplayConfig>::iterator dispConf = displays.begin(); dispConf != displays.end(); dispConf++)
   {
      if (display == -1 && dispConf->isPrimary)
         display = dispConf->display;
      char displayName[256];
      sprintf_s(displayName, "Display %d%s %dx%d %s", dispConf->display + 1, (dispConf->isPrimary) ? "*" : "", dispConf->width, dispConf->height, dispConf->GPU_Name);
      SendMessage(GetDlgItem(IDC_DISPLAY_ID).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)displayName);
   }
   SendMessage(GetDlgItem(IDC_DISPLAY_ID).GetHwnd(), CB_SETCURSEL, display, 0);

   const int widthcur = LoadValueIntWithDefault("PlayerVR", "Width", DEFAULT_PLAYER_WIDTH);

   const int heightcur = LoadValueIntWithDefault("PlayerVR", "Height", widthcur * 9 / 16);

   SendMessage(hwndDlg, GET_WINDOW_MODES, widthcur, heightcur);

   //AMD Debugging
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGB 8");
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGBA 8");
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGB 16F");
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"RGBA 16F");
   int textureModeVR = LoadValueIntWithDefault("Player", "textureModeVR", 1);
   SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_SETCURSEL, textureModeVR, 0);

   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Blit");
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"BlitNamed");
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_ADDSTRING, 0, (LPARAM)"Shader");
   int blitModeVR = LoadValueIntWithDefault("Player", "blitModeVR", 0);
   SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_SETCURSEL, blitModeVR, 0);

   return TRUE;
}

INT_PTR VROptionsDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   switch (uMsg)
   {
   case GET_WINDOW_MODES:
   {
      size_t indexcur = -1;
      size_t indx = -1;
      int widthcur = (int)wParam, heightcur = (int)lParam;

      SendMessage(GetHwnd(), RESET_SIZELIST_CONTENT, 0, 0);
      HWND hwndList = GetDlgItem(IDC_SIZELIST).GetHwnd();
      //indx = SendMessage(hwndList, LB_GETCURSEL, 0L, 0L);
      //if (indx == LB_ERR)
      //  indx = 0;

      const size_t csize = sizeof(rgwindowsize) / sizeof(int);
      int screenwidth;
      int screenheight;
      int x, y;
      const int display = (int)SendMessage(GetDlgItem(IDC_DISPLAY_ID).GetHwnd(), CB_GETCURSEL, 0, 0);
      getDisplaySetupByID(display, x, y, screenwidth, screenheight);

      //if (indx != -1)
      //  indexcur = indx;

      allVideoModes.clear();
      size_t cnt = 0;

      // test video modes first on list

      // add some (windowed) portrait play modes

      // 16:10 aspect ratio resolutions: 1280*800, 1440*900, 1680*1050, 1920*1200 and 2560*1600
      // 16:9 aspect ratio resolutions:  1280*720, 1366*768, 1600*900, 1920*1080, 2560*1440 and 3840*2160
      // 21:9 aspect ratio resolution:   3440*1440
      // 4:3  aspect ratio resolutions:  1280*1024
      const unsigned int num_portrait_modes = 15;
      const int portrait_modes_width[num_portrait_modes] = { 720, 720, 1024, 768, 800, 900, 900,1050,1050,1080,1200,1440,1440,1600,2160 };
      const int portrait_modes_height[num_portrait_modes] = { 1024,1280, 1280,1366,1280,1440,1600,1600,1680,1920,1920,2560,3440,2560,3840 };

      for (unsigned int i = 0; i < num_portrait_modes; ++i)
         if ((portrait_modes_width[i] <= screenwidth) && (portrait_modes_height[i] <= screenheight))
         {
            VideoMode mode;
            mode.width = portrait_modes_width[i];
            mode.height = portrait_modes_height[i];
            mode.depth = 0;
            mode.refreshrate = 0;

            allVideoModes.push_back(mode);
            if (heightcur > widthcur)
               if ((portrait_modes_width[i] == widthcur) && (portrait_modes_height[i] == heightcur))
                  indx = i;
            cnt++;
         }

      // add landscape play modes

      for (size_t i = 0; i < csize; ++i)
      {
         const int xsize = rgwindowsize[i];
         if ((xsize <= screenwidth) && ((xsize * 3 / 4) <= screenheight))
         {
            if ((xsize == widthcur) && ((xsize * 3 / 4) == heightcur))
               indx = i + cnt;

            VideoMode mode;
            mode.width = xsize;
            mode.height = xsize * 3 / 4;
            mode.depth = 0;
            mode.refreshrate = 0;

            allVideoModes.push_back(mode);
         }
      }

      FillVideoModesList(allVideoModes);

      // set up windowed fullscreen mode
      VideoMode mode;
      mode.width = screenwidth;
      mode.height = screenheight;
      mode.depth = 0;
      mode.refreshrate = 0;
      allVideoModes.push_back(mode);

      char szT[128];
      //if (indexcur == -1)
      //  indexcur = indx;

      if (mode.height < mode.width) // landscape
      {
         sprintf_s(szT, "%d x %d (Windowed Fullscreen)", mode.width, mode.height);
         SendMessage(hwndList, LB_ADDSTRING, 0, (LPARAM)szT);
         if (indx == -1)
            indexcur = SendMessage(hwndList, LB_GETCOUNT, 0, 0) - 1;
         else
            indexcur = indx;
      }
      else { // portrait
         if ((indx == -1) || (indx < num_portrait_modes))
         {
            indexcur = indx;
            if (cnt > 0)
            {
               char szTx[128];
               SendMessage(hwndList, LB_GETTEXT, cnt - 1, (LPARAM)szTx);
               SendMessage(hwndList, LB_DELETESTRING, cnt - 1, 0L);

               if (cnt - 1 < num_portrait_modes)
               {
                  mode.width = portrait_modes_width[cnt - 1];
                  mode.height = portrait_modes_height[cnt - 1];

                  if ((mode.height == screenheight) && (mode.width == screenwidth))
                     sprintf_s(szT, "%d x %d (Windowed Fullscreen)", mode.width, mode.height);
                  else
                     sprintf_s(szT, "%d x %d", mode.width, mode.height);
               }
               else {
                  memset(&szTx, '\x0', sizeof(szTx));
                  strcpy_s(szT, szTx);
               }

               SendMessage(hwndList, LB_INSERTSTRING, cnt - 1, (LPARAM)szT);
            } // end if cnt > 0
         } // end if indx
      } // end if else mode height < width

      SendMessage(hwndList, LB_SETCURSEL, (indexcur != -1) ? indexcur : 0, 0);
      break;
   } // end case GET_WINDOW_MODES
   case RESET_SIZELIST_CONTENT:
   {
      HWND hwndList = GetDlgItem(IDC_SIZELIST).GetHwnd();
      SendMessage(hwndList, LB_RESETCONTENT, 0, 0);
      break;
   }
   case WM_HSCROLL:
   {
      if ((HWND)lParam == GetDlgItem(IDC_SSSLIDER).GetHwnd()) {
         const size_t pos = SendMessage(GetDlgItem(IDC_SSSLIDER).GetHwnd(), TBM_GETPOS, 0, 0);//Reading the value from wParam does not work reliable
         const float AAfactor = ((pos) < AAfactorCount) ? AAfactors[pos] : 1.0f;
         char newText[32];
         sprintf_s(newText, "Supersampling Factor: %.2f", AAfactor);
         SetDlgItemText(IDC_SSSLIDER_LABEL, newText);
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
   case IDC_DISPLAY_ID:
   {
      size_t index = (int)SendMessage(GetDlgItem(IDC_SIZELIST).GetHwnd(), LB_GETCURSEL, 0, 0);
      if (allVideoModes.size() == 0) {
         HWND hwndList = GetDlgItem(IDC_SIZELIST).GetHwnd();
         HWND hwndDisplay = GetDlgItem(IDC_DISPLAY_ID).GetHwnd();
         int display = SendMessage(hwndDisplay, CB_GETCURSEL, 0, 0);
         EnumerateDisplayModes(display, allVideoModes);

      }
      if (allVideoModes.size() > index) {
         VideoMode * pvm = &allVideoModes[index];
         SendMessage(GET_WINDOW_MODES, pvm->width, pvm->height);
      }
      else
         SendMessage(GET_WINDOW_MODES, 0, 0);
      break;
   }
   case IDC_SCALE_TO_CM:
   {
      const bool newScaleValue = SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_GETCHECK, 0, 0) > 0;
      if (oldScaleValue != newScaleValue) {
         CString tmpStr = GetDlgItemTextA(IDC_VR_SCALE);
         tmpStr.Replace(',', '.');
         char tmp[256];
         if (oldScaleValue)
            scaleAbsolute = atof(tmpStr.c_str());
         else
            scaleRelative = atof(tmpStr.c_str());

         sprintf_s(tmp, 256, newScaleValue ? "%0.1f" : "%0.3f", newScaleValue ? scaleAbsolute : scaleRelative);
         SetDlgItemTextA(IDC_VR_SCALE, tmp);
         oldScaleValue = newScaleValue;
      }
   }
   default:
      return FALSE;
   }
   return TRUE;
}

void VROptionsDialog::OnOK()
{
   BOOL nothing = 0;

   size_t index = SendMessage(GetDlgItem(IDC_SIZELIST).GetHwnd(), LB_GETCURSEL, 0, 0);
   VideoMode* pvm = &allVideoModes[index];
   SaveValueInt("PlayerVR", "Width", pvm->width);
   SaveValueInt("PlayerVR", "Height", pvm->height);

   size_t display = SendMessage(GetDlgItem(IDC_DISPLAY_ID).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt("PlayerVR", "Display", display);

   CString tmpStr;

   tmpStr = GetDlgItemTextA(IDC_NUDGE_STRENGTH);
   SaveValueString("PlayerVR", "NudgeStrength", tmpStr.c_str());

   size_t fxaa = SendMessage(GetDlgItem(IDC_FXAACB).GetHwnd(), CB_GETCURSEL, 0, 0);
   if (fxaa == LB_ERR)
      fxaa = 0;
   SaveValueInt("PlayerVR", "FXAA", fxaa);

   size_t scaleFX_DMD = SendMessage(GetDlgItem(IDC_SCALE_FX_DMD).GetHwnd(), BM_GETCHECK, 0, 0);
   SaveValueInt("PlayerVR", "ScaleFXDMD", scaleFX_DMD);

   const size_t AAfactorIndex = SendMessage(GetDlgItem(IDC_SSSLIDER).GetHwnd(), TBM_GETPOS, 0, 0);
   const float AAfactor = (AAfactorIndex < AAfactorCount) ? AAfactors[AAfactorIndex] : 1.0f;
   SaveValueFloat("PlayerVR", "AAFactor", AAfactor);

   size_t useAO = SendMessage(GetDlgItem(IDC_DYNAMIC_AO).GetHwnd(), BM_GETCHECK, 0, 0);
   SaveValueInt("PlayerVR", "DynamicAO", useAO);

   useAO = SendMessage(GetDlgItem(IDC_ENABLE_AO).GetHwnd(), BM_GETCHECK, 0, 0) ? 0 : 1; // inverted logic
   SaveValueInt("PlayerVR", "DisableAO", useAO);

   const size_t ssreflection = SendMessage(GetDlgItem(IDC_GLOBAL_SSREFLECTION_CHECK).GetHwnd(), BM_GETCHECK, 0, 0);
   SaveValueInt("PlayerVR", "SSRefl", ssreflection);

   const size_t pfreflection = SendMessage(GetDlgItem(IDC_GLOBAL_PFREFLECTION_CHECK).GetHwnd(), BM_GETCHECK, 0, 0);
   SaveValueInt("PlayerVR", "PFRefl", pfreflection);

   //AMD Debugging
   const size_t textureModeVR = SendMessage(GetDlgItem(IDC_COMBO_TEXTURE).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt("Player", "textureModeVR", textureModeVR);

   const size_t blitModeVR = SendMessage(GetDlgItem(IDC_COMBO_BLIT).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt("Player", "blitModeVR", blitModeVR);

   const size_t disableVRPreview = SendMessage(GetDlgItem(IDC_VR_DISABLE_PREVIEW).GetHwnd(), BM_GETCHECK, 0, 0);
   SaveValueInt("PlayerVR", "VRPreviewDisabled", disableVRPreview);

   const size_t scaleToFixedWidth = SendMessage(GetDlgItem(IDC_SCALE_TO_CM).GetHwnd(), BM_GETCHECK, 0, 0);
   SaveValueInt("PlayerVR", "scaleToFixedWidth", scaleToFixedWidth);

   tmpStr = GetDlgItemTextA(IDC_VR_SCALE);
   SaveValueString("PlayerVR", scaleToFixedWidth ? "scaleAbsolute" : "scaleRelative", tmpStr.c_str());
   //SaveValueFloat("PlayerVR", scaleToFixedWidth ? "scaleRelative" : "scaleAbsolute", scaleToFixedWidth ? scaleRelative : scaleAbsolute); //Also update hidden value?

   tmpStr = GetDlgItemTextA(IDC_NEAR_PLANE);
   SaveValueString("PlayerVR", "nearPlane", tmpStr.c_str());

   tmpStr = GetDlgItemTextA(IDC_FAR_PLANE);
   SaveValueString("PlayerVR", "farPlane", tmpStr.c_str());

   //For compatibility keep these in Player instead of PlayerVR
   tmpStr = GetDlgItemTextA(IDC_VR_SLOPE);
   SaveValueString("Player", "VRSlope", tmpStr.c_str());

   tmpStr = GetDlgItemTextA(IDC_3D_VR_ORIENTATION);
   SaveValueString("Player", "VROrientation", tmpStr.c_str());

   tmpStr = GetDlgItemTextA(IDC_VR_OFFSET_X);
   SaveValueString("Player", "VRTableX", tmpStr.c_str());

   tmpStr = GetDlgItemTextA(IDC_VR_OFFSET_Y);
   SaveValueString("Player", "VRTableY", tmpStr.c_str());

   tmpStr = GetDlgItemTextA(IDC_VR_OFFSET_Z);
   SaveValueString("Player", "VRTableZ", tmpStr.c_str());

   const size_t bloomOff = SendMessage(GetDlgItem(IDC_BLOOM_OFF).GetHwnd(), BM_GETCHECK, 0, 0);
   SaveValueInt("PlayerVR", "ForceBloomOff", bloomOff);
   
   const size_t askToTurnOn = SendMessage(GetDlgItem(IDC_TURN_VR_ON).GetHwnd(), CB_GETCURSEL, 0, 0);
   SaveValueInt("PlayerVR", "AskToTurnOn", askToTurnOn);

   CDialog::OnOK();

}

void VROptionsDialog::OnClose()
{
   SendMessage(RESET_SIZELIST_CONTENT, 0, 0);
   CDialog::OnClose();
}
