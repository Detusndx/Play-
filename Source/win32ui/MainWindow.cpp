#include <stdio.h>
#include <boost/lexical_cast.hpp>
#include <iomanip>
#include "MainWindow.h"
#include "PtrMacro.h"
#include "../PS2VM.h"
#include "../PS2OS.h"
#include "../Config.h"
#include "GSH_OpenGLWin32.h"
#include "GSH_Software.h"
#include "../GSH_Null.h"
#include "PH_DirectInput.h"
#include "win32/FileDialog.h"
#include "VFSManagerWnd.h"
#include "McManagerWnd.h"
#include "Debugger.h"
#include "SysInfoWnd.h"
#include "AboutWnd.h"
#include "../Profiler.h"
#include "resource.h"
#include "string_cast.h"

#define CLSNAME						_T("MainWindow")
#define WNDSTYLE					(WS_CLIPCHILDREN | WS_DLGFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)

#define STATUSPANEL					0
#define FPSPANEL					1

#define TITLEA						"Purei!"
#define TITLEW						L"プレイ!"

#define VMMENUPOS					1

#define ID_MAIN_VM_STATESLOT_0		(0xBEEF)
#define MAX_STATESLOTS				10

#define ID_MAIN_DEBUG_SHOW			(0xDEAD)

#define PREF_UI_PAUSEWHENFOCUSLOST	"ui.pausewhenfocuslost"

using namespace Framework;
using namespace std;
using namespace boost;

double CMainWindow::m_nStatusBarPanelWidths[2] =
{
	0.7,
	0.3,
};

CMainWindow::CMainWindow(CPS2VM& virtualMachine, char* sCmdLine) :
m_virtualMachine(virtualMachine)
{
	RECT rc;
	TCHAR sVersion[256];

	CConfig::GetInstance()->RegisterPreferenceBoolean(PREF_UI_PAUSEWHENFOCUSLOST, true);

	if(!DoesWindowClassExist(CLSNAME))
	{
		WNDCLASSEX wc;
		memset(&wc, 0, sizeof(WNDCLASSEX));
		wc.cbSize			= sizeof(WNDCLASSEX);
		wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground	= (HBRUSH)(COLOR_WINDOW); 
		wc.hInstance		= GetModuleHandle(NULL);
		wc.lpszClassName	= CLSNAME;
		wc.lpfnWndProc		= CWindow::WndProc;
		wc.style			= CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
		RegisterClassEx(&wc);
	}

	SetRect(&rc, 0, 0, 640, 480);
	
	Create(NULL, CLSNAME, _T(""), WNDSTYLE, &rc, NULL, NULL);
	SetClassPtr();

#ifdef DEBUGGER_INCLUDED
	CDebugger::InitializeConsole();
#endif

    m_virtualMachine.Initialize();

	SetIcon(ICON_SMALL, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_PUREI)));
	SetIcon(ICON_BIG, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_PUREI)));

	SetMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINWINDOW)));

#ifdef DEBUGGER_INCLUDED
	m_pDebugger = new CDebugger(m_virtualMachine);
	CreateDebugMenu();
#endif

	PrintVersion(sVersion, countof(sVersion));

	SetRect(&rc, 0, 0, 1, 1);
	m_pOutputWnd = new COutputWnd(m_hWnd, &rc);

	m_pStatusBar = new Win32::CStatusBar(m_hWnd);
	m_pStatusBar->SetParts(2, m_nStatusBarPanelWidths);
	m_pStatusBar->SetText(STATUSPANEL,	sVersion);
	m_pStatusBar->SetText(FPSPANEL,		_T("0 fps"));

	m_pOnOutputWndSizeChangeHandler = new CEventHandlerMethod<CMainWindow, int>(this, &CMainWindow::OnOutputWndSizeChange);
	m_pOutputWnd->m_OnSizeChange.InsertHandler(m_pOnOutputWndSizeChangeHandler);

#ifdef AMD64
//    CGSH_Null::CreateGSHandler();
    CGSH_Software::CreateGSHandler(m_virtualMachine, m_pOutputWnd);
#else
//    CGSH_Null::CreateGSHandler();
    CGSH_OpenGLWin32::CreateGSHandler(m_virtualMachine, m_pOutputWnd);
//    CGSH_Software::CreateGSHandler(m_virtualMachine, m_pOutputWnd);
#endif

	CPH_DirectInput::CreatePadHandler(m_virtualMachine, m_hWnd);

	//Initialize FPS counter
	m_nFrames = 0;

	m_nStateSlot = 0;

	m_nDeactivatePause = false;
	m_nPauseFocusLost = CConfig::GetInstance()->GetPreferenceBoolean(PREF_UI_PAUSEWHENFOCUSLOST);

	m_virtualMachine.m_pGS->OnNewFrame.connect(bind(&CMainWindow::OnNewFrame, this));

	SetTimer(m_hWnd, NULL, 1000, NULL);

	m_virtualMachine.m_os->m_OnExecutableChange.connect(bind(&CMainWindow::OnExecutableChange, this));

	CreateStateSlotMenu();
	CreateAccelerators();

	if(strstr(sCmdLine, "-cdrom0") != NULL)
	{
		BootCDROM();
	}
	else if(strlen(sCmdLine))
	{
		LoadELF(sCmdLine);
	}

	UpdateUI();
	Show(SW_SHOW);

#if (_DEBUG && DEBUGGER_INCLUDED)
	ShowDebugger();
#endif
}

CMainWindow::~CMainWindow()
{
	m_virtualMachine.Pause();

	m_virtualMachine.DestroyPadHandler();
	m_virtualMachine.DestroyGSHandler();

#ifdef DEBUGGER_INCLUDED
	DELETEPTR(m_pDebugger);
#endif

	m_pOutputWnd->m_OnSizeChange.RemoveHandler(m_pOnOutputWndSizeChangeHandler);

	DELETEPTR(m_pOutputWnd);
	DELETEPTR(m_pStatusBar);

	DestroyAcceleratorTable(m_nAccTable);

	m_virtualMachine.Destroy();
}

int CMainWindow::Loop()
{
	MSG msg;
	HWND hActive;
	bool nDispatched;

	while(IsWindow())
	{
		GetMessage(&msg, NULL, 0, 0);
		nDispatched = false;
		hActive = GetActiveWindow();

		if(hActive == m_hWnd)
		{
			nDispatched = TranslateAccelerator(m_hWnd, m_nAccTable, &msg) != 0;
		}
#ifdef DEBUGGER_INCLUDED
		else if(hActive == m_pDebugger->m_hWnd)
		{
			nDispatched = TranslateAccelerator(m_pDebugger->m_hWnd, m_pDebugger->GetAccelerators(), &msg) != 0;
		}
#endif
		if(!nDispatched)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return 0;
}

long CMainWindow::OnCommand(unsigned short nID, unsigned short nCmd, HWND hSender)
{
	switch(nID)
	{
	case ID_MAIN_FILE_LOADELF:
		OpenELF();
		break;
	case ID_MAIN_FILE_BOOTCDROM:
		BootCDROM();
		break;
	case ID_MAIN_FILE_EXIT:
		DestroyWindow(m_hWnd);
		break;
	case ID_MAIN_VM_RESUME:
		ResumePause();
		break;
	case ID_MAIN_VM_RESET:
		Reset();
		break;
	case ID_MAIN_VM_PAUSEFOCUS:
		PauseWhenFocusLost();
		break;
	case ID_MAIN_VM_SAVESTATE:
		SaveState();
		break;
	case ID_MAIN_VM_LOADSTATE:
		LoadState();
		break;
	case ID_MAIN_VM_STATESLOT_0 + 0:
	case ID_MAIN_VM_STATESLOT_0 + 1:
	case ID_MAIN_VM_STATESLOT_0 + 2:
	case ID_MAIN_VM_STATESLOT_0 + 3:
	case ID_MAIN_VM_STATESLOT_0 + 4:
	case ID_MAIN_VM_STATESLOT_0 + 5:
	case ID_MAIN_VM_STATESLOT_0 + 6:
	case ID_MAIN_VM_STATESLOT_0 + 7:
	case ID_MAIN_VM_STATESLOT_0 + 8:
	case ID_MAIN_VM_STATESLOT_0 + 9:
		ChangeStateSlot(nID - ID_MAIN_VM_STATESLOT_0);
		break;
	case ID_MAIN_OPTIONS_RENDERER:
		ShowRendererSettings();
		break;
	case ID_MAIN_OPTIONS_VFSMANAGER:
		ShowVfsManager();
		break;
	case ID_MAIN_OPTIONS_MCMANAGER:
		ShowMcManager();
		break;
	case ID_MAIN_DEBUG_SHOW:
		ShowDebugger();
		break;
	case ID_MAIN_HELP_SYSINFO:
		ShowSysInfo();
		break;
	case ID_MAIN_HELP_ABOUT:
		ShowAbout();
		break;

	}
	return TRUE;
}

long CMainWindow::OnTimer()
{
	tstring sCaption;

	sCaption = lexical_cast<tstring>(m_nFrames) + _T(" fps");
	m_pStatusBar->SetText(FPSPANEL, sCaption.c_str());

	m_nFrames = 0;

#ifdef PROFILE

	CProfiler::ZoneList Zones;
	double nTotalTime;
	xstringstream sProfileCaption;

	Zones = CProfiler::GetInstance().GetStats();
	CProfiler::GetInstance().Reset();

	nTotalTime = 0;

	for(CProfiler::ZoneList::iterator itZone = Zones.begin();
		itZone != Zones.end();
		itZone++)
	{
		nTotalTime += (*itZone).GetTime();		
	}

	sProfileCaption.precision(4);

	for(CProfiler::ZoneList::iterator itZone = Zones.begin();
		itZone != Zones.end();
		itZone++)
	{
		sProfileCaption << (*itZone).GetName() << ": " << setw(5) << ((double)(*itZone).GetTime() / nTotalTime) * 100 << " ";
	}

	m_pStatusBar->SetText(STATUSPANEL, sProfileCaption.str().c_str());


#endif

	return TRUE;
}

long CMainWindow::OnActivateApp(bool nActive, unsigned long nThreadId)
{
	if(m_nPauseFocusLost == true)
	{
		if(nActive == false)
		{
            if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
			{
				ResumePause();
				m_nDeactivatePause = true;
			}
		}
		
		if((nActive == true) && (m_nDeactivatePause == true))
		{
			ResumePause();
			m_nDeactivatePause = false;
		}
	}
	return FALSE;
}

void CMainWindow::OpenELF()
{
	Win32::CFileDialog d;
	int nRet;

	d.m_OFN.lpstrFilter = _T("ELF Executable Files (*.elf)\0*.elf\0All files (*.*)\0*.*\0");

	Enable(FALSE);
	nRet = d.Summon(m_hWnd);
	Enable(TRUE);
	SetFocus();

	if(nRet == 0) return;

	LoadELF(string_cast<string>(d.m_sFile).c_str());
}

void CMainWindow::ResumePause()
{
    if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		m_virtualMachine.Pause();
		SetStatusText(_T("Virtual Machine paused."));
	}
	else
	{
		m_virtualMachine.Resume();
		SetStatusText(_T("Virtual Machine resumed."));
	}
}

void CMainWindow::Reset()
{
	//Calling this will clear the RAM completely and remove the executable from memory...
	
	//CPS2VM::Reset();
	//SetStatusText(_X("Virtual Machine reset."));
}

void CMainWindow::PauseWhenFocusLost()
{
	m_nPauseFocusLost = !m_nPauseFocusLost;
	if(m_nPauseFocusLost)
	{
		m_nDeactivatePause = false;
	}

	CConfig::GetInstance()->SetPreferenceBoolean(PREF_UI_PAUSEWHENFOCUSLOST, m_nPauseFocusLost);
	UpdateUI();
}

void CMainWindow::SaveState()
{
	if(m_virtualMachine.m_os->GetELF() == NULL) return;

	if(m_virtualMachine.SaveState(GenerateStatePath().c_str()) == 0)
	{
		PrintStatusTextA("Saved state to slot %i.", m_nStateSlot);
	}
	else
	{
		PrintStatusTextA("Error saving state to slot %i.", m_nStateSlot);
	}
}

void CMainWindow::LoadState()
{
	if(m_virtualMachine.m_os->GetELF() == NULL) return;

	if(m_virtualMachine.LoadState(GenerateStatePath().c_str()) == 0)
	{
		PrintStatusTextA("Loaded state from slot %i.", m_nStateSlot);
	}
	else
	{
		PrintStatusTextA("Error loading state from slot %i.", m_nStateSlot);
	}
}

void CMainWindow::ChangeStateSlot(unsigned int nSlot)
{
	m_nStateSlot = nSlot % MAX_STATESLOTS;
	UpdateUI();
}

void CMainWindow::ShowDebugger()
{
#ifdef DEBUGGER_INCLUDED
	m_pDebugger->Show(SW_MAXIMIZE);
	SetForegroundWindow(m_pDebugger->m_hWnd);
#endif
}

void CMainWindow::ShowSysInfo()
{
	CSysInfoWnd SysInfoWnd(m_hWnd);

	SysInfoWnd.Center();
	SysInfoWnd.Show(SW_SHOW);
	CWindow::StdMsgLoop(&SysInfoWnd);

	Redraw();
}

void CMainWindow::ShowAbout()
{
	CAboutWnd AboutWnd(m_hWnd);

	AboutWnd.Center();
	AboutWnd.Show(SW_SHOW);
	CWindow::StdMsgLoop(&AboutWnd);

	Redraw();
}

void CMainWindow::ShowRendererSettings()
{
	bool nPaused;
	CModalWindow* pWindow;
	CSettingsDialogProvider* pProvider;

	nPaused = false;

    if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		nPaused = true;
		ResumePause();
	}

	pProvider = dynamic_cast<CSettingsDialogProvider*>(m_virtualMachine.GetGSHandler());
	if(pProvider != NULL)
	{
		pWindow = pProvider->CreateSettingsDialog(m_hWnd);
		pWindow->DoModal();
		DELETEPTR(pWindow);
		pProvider->OnSettingsDialogDestroyed();
	}

	Redraw();

	if(nPaused)
	{
		ResumePause();
	}
}

void CMainWindow::ShowVfsManager()
{
	bool nPaused;

	nPaused = false;

    if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		nPaused = true;
		ResumePause();
	}

	CVFSManagerWnd VFSManagerWnd(m_hWnd);
	VFSManagerWnd.DoModal();

	Redraw();

	if(nPaused)
	{
		ResumePause();
	}
}

void CMainWindow::ShowMcManager()
{
	bool nPaused;

	nPaused = false;

    if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		nPaused = true;
		ResumePause();
	}

	CMcManagerWnd McManagerWnd(m_hWnd);
	McManagerWnd.DoModal();

	Redraw();

	if(nPaused)
	{
		ResumePause();
	}	
}

void CMainWindow::LoadELF(const char* sFilename)
{
    CPS2OS& os = *m_virtualMachine.m_os;
	m_virtualMachine.Reset();

	try
	{
		os.BootFromFile(sFilename);
#if !defined(_DEBUG) && !defined(DEBUGGER_INCLUDED)
		m_virtualMachine.Resume();
#endif
		PrintStatusTextA("Loaded executable '%s'.", os.GetExecutableName());
	}
	catch(const exception& Exception)
	{
		MessageBoxA(m_hWnd, Exception.what(), NULL, 16);
	}
}

void CMainWindow::BootCDROM()
{
    CPS2OS& os = *m_virtualMachine.m_os;
    m_virtualMachine.Reset();

	try
	{
		os.BootFromCDROM();
#ifndef _DEBUG
		m_virtualMachine.Resume();
#endif
		PrintStatusTextA("Loaded executable '%s' from cdrom0.", os.GetExecutableName());
	}
	catch(const exception& Exception)
	{
		MessageBoxA(m_hWnd, Exception.what(), NULL, 16);
	}
}

void CMainWindow::RefreshLayout()
{
	RECT rc;
	unsigned int nViewW, nViewH;

	m_pOutputWnd->GetWindowRect(&rc);

	nViewW = rc.right - rc.left;
	nViewH = rc.bottom - rc.top;

	SetRect(&rc, 0, 0, nViewW, nViewH);
	rc.bottom += m_pStatusBar->GetHeight();

	AdjustWindowRect(&rc, WNDSTYLE, TRUE);

	SetSize(rc.right - rc.left, rc.bottom - rc.top);

	m_pStatusBar->RefreshGeometry();
	m_pStatusBar->SetParts(2, m_nStatusBarPanelWidths);

	//m_pStatusBar->SetPosition(0, nViewH + 2);
	//m_pStatusBar->SetSize(nViewW, 21);

	Center();
}

void CMainWindow::PrintStatusTextA(const char* sFormat, ...)
{
	char sText[256];
	va_list Args;

	va_start(Args, sFormat);
	_vsnprintf(sText, 256, sFormat, Args);
	va_end(Args);

	m_pStatusBar->SetText(STATUSPANEL, string_cast<tstring>(sText).c_str());
}

void CMainWindow::SetStatusText(const TCHAR* sText)
{
	m_pStatusBar->SetText(STATUSPANEL, sText);
}

void CMainWindow::CreateAccelerators()
{
	ACCEL Accel[4];

	Accel[0].cmd	= ID_MAIN_VM_RESUME;
	Accel[0].key	= VK_F5;
	Accel[0].fVirt	= FVIRTKEY;

	Accel[1].cmd	= ID_MAIN_FILE_LOADELF;
	Accel[1].key	= 'O';
	Accel[1].fVirt	= FVIRTKEY | FCONTROL;

	Accel[2].cmd	= ID_MAIN_VM_SAVESTATE;
	Accel[2].key	= VK_F7;
	Accel[2].fVirt	= FVIRTKEY;

	Accel[3].cmd	= ID_MAIN_VM_LOADSTATE;
	Accel[3].key	= VK_F8;
	Accel[3].fVirt	= FVIRTKEY;

	m_nAccTable = CreateAcceleratorTable(Accel, sizeof(Accel) / sizeof(ACCEL));
}

void CMainWindow::CreateDebugMenu()
{
	HMENU hMenu;
	MENUITEMINFO ItemInfo;

	hMenu = CreatePopupMenu();
	InsertMenu(hMenu, 0, MF_STRING, ID_MAIN_DEBUG_SHOW, _T("Show Debugger"));

	memset(&ItemInfo, 0, sizeof(MENUITEMINFO));
	ItemInfo.cbSize		= sizeof(MENUITEMINFO);
	ItemInfo.fMask		= MIIM_STRING | MIIM_SUBMENU;
	ItemInfo.dwTypeData	= _T("Debug");
	ItemInfo.hSubMenu	= hMenu;

	InsertMenuItem(GetMenu(m_hWnd), 3, TRUE, &ItemInfo);
}

void CMainWindow::CreateStateSlotMenu()
{
	HMENU hMenu;
	MENUITEMINFO ItemInfo;
	unsigned int i;

	hMenu = CreatePopupMenu();
	for(i = 0; i < MAX_STATESLOTS; i++)
	{
		tstring sCaption;
		sCaption = _T("Slot ") + lexical_cast<tstring>(i);
		InsertMenu(hMenu, i, MF_STRING, ID_MAIN_VM_STATESLOT_0 + i, sCaption.c_str());
	}

	memset(&ItemInfo, 0, sizeof(MENUITEMINFO));
	ItemInfo.cbSize		= sizeof(MENUITEMINFO);
	ItemInfo.fMask		= MIIM_SUBMENU;
	ItemInfo.hSubMenu	= hMenu;

	hMenu = GetSubMenu(GetMenu(m_hWnd), VMMENUPOS);
	SetMenuItemInfo(hMenu, ID_MAIN_VM_STATESLOT, FALSE, &ItemInfo);
}

string CMainWindow::GenerateStatePath()
{
	return string("./states/") + 
        m_virtualMachine.m_os->GetExecutableName() + ".st" + 
        lexical_cast<string>(m_nStateSlot) + ".zip";
}

void CMainWindow::UpdateUI()
{
	HMENU hMenu;
	const char* sExec;
	TCHAR sTitle[256];
	MENUITEMINFO MenuItem;
	unsigned int i;

	//Fix the virtual machine sub menu
	hMenu = GetSubMenu(GetMenu(m_hWnd), VMMENUPOS);

    CPS2OS& os = *m_virtualMachine.m_os;
    bool hasElf = (os.GetELF() != NULL);

	EnableMenuItem(hMenu, ID_MAIN_VM_RESUME, (!hasElf ? MF_GRAYED : 0) | MF_BYCOMMAND);
//	EnableMenuItem(hMenu, ID_MAIN_VM_RESET, (!hasElf ? MF_GRAYED : 0) | MF_BYCOMMAND);
	CheckMenuItem(hMenu, ID_MAIN_VM_PAUSEFOCUS, (m_nPauseFocusLost ? MF_CHECKED : MF_UNCHECKED) | MF_BYCOMMAND);
	EnableMenuItem(hMenu, ID_MAIN_VM_SAVESTATE, (!hasElf ? MF_GRAYED : 0) | MF_BYCOMMAND);
	EnableMenuItem(hMenu, ID_MAIN_VM_LOADSTATE, (!hasElf ? MF_GRAYED : 0) | MF_BYCOMMAND);

	//Get state slot sub-menu
	memset(&MenuItem, 0, sizeof(MENUITEMINFO));
	MenuItem.cbSize = sizeof(MENUITEMINFO);
	MenuItem.fMask	= MIIM_SUBMENU;

	GetMenuItemInfo(hMenu, ID_MAIN_VM_STATESLOT, FALSE, &MenuItem);
	hMenu = MenuItem.hSubMenu;

	//Change state slot number checkbox
	for(i = 0; i < MAX_STATESLOTS; i++)
	{
		memset(&MenuItem, 0, sizeof(MENUITEMINFO));
		MenuItem.cbSize = sizeof(MENUITEMINFO);
		MenuItem.fMask	= MIIM_STATE;
		MenuItem.fState	= (m_nStateSlot == i) ? MFS_CHECKED : MFS_UNCHECKED;

		SetMenuItemInfo(hMenu, ID_MAIN_VM_STATESLOT_0 + i, FALSE, &MenuItem);
	}

    sExec = os.GetExecutableName();
	if(strlen(sExec))
	{
		_sntprintf(sTitle, countof(sTitle), _T("%s - [ %s ]"), tcond(TITLEA, TITLEW), string_cast<tstring>(sExec).c_str());
	}
	else
	{
		_sntprintf(sTitle, countof(sTitle), _T("%s"), tcond(TITLEA, TITLEW));
	}

	SetText(sTitle);
}

void CMainWindow::PrintVersion(TCHAR* sVersion, size_t nCount)
{
	_sntprintf(sVersion, nCount, _T("プレイ! v%i.%0.2i - %s"), VERSION_MAJOR, VERSION_MINOR, string_cast<tstring>(__DATE__).c_str());
}

void CMainWindow::OnOutputWndSizeChange(int nNothing)
{
	RefreshLayout();
}

void CMainWindow::OnNewFrame()
{
	m_nFrames++;
}

void CMainWindow::OnExecutableChange()
{
	UpdateUI();
}
