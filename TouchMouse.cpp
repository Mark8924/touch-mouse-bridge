#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <wtsapi32.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <iterator>
#include <cwctype>
#include "engine.hpp"
using namespace touchmouse;
constexpr ULONG_PTR OWN_MARK=0x4e414255u;
constexpr ULONG_PTR TEST_TOUCH=0xff515780u;
constexpr UINT M_CONFIG=WM_APP+1,M_STOP=WM_APP+2,M_WAKE=WM_APP+3,M_SHOW=WM_APP+4,M_TRAY=WM_APP+5,M_HIDE=WM_APP+6;
constexpr UINT TRAY_SHOW=400,TRAY_TOGGLE=401,TRAY_AUTOSTART=402,TRAY_EXIT=403;
constexpr GUID TRAY_GUID={0x5fa8e456,0x4cf0,0x4ade,{0x91,0x15,0x7d,0x4f,0x29,0x67,0x8d,0xb0}};
constexpr wchar_t MAIN_CLASS[]=L"TouchMouseBridge.Main.v1";
struct Profile { std::wstring path; bool enabled=true; };
struct Config { bool enabled=false,global=false,autostart=false; int hold=60,mode=1; std::vector<Profile> games; };
struct Status {std::atomic<unsigned long long> touches{0},clicks{0},rightClicks{0},dropped{0},errors{0},revision{0};std::atomic<bool> active{false};std::mutex mutex;std::wstring target=L"未检测到目标游戏",error; } status;
Config config;
HINSTANCE instance;
HWND mainWindow=nullptr,testWindow=nullptr;
std::string testTrace;
DWORD workerId=0;
HANDLE workerHandle=nullptr,readyEvent=nullptr;
std::wstring configPath;
bool rebuilding=false,backgroundStart=false;
UINT dpi=96;
HFONT normalFont=nullptr,titleFont=nullptr;
HBRUSH backgroundBrush=nullptr;
HICON trayIcon=nullptr;
bool trayAdded=false;
UINT taskbarCreated=0;
std::wstring trayTestOutput;
std::string trayTestFailure;
int trayTestPhase=0;
void updateTray();
std::wstring executable(){wchar_t p[32768];DWORD n=GetModuleFileNameW(nullptr,p,32768);return std::wstring(p,n);}
std::wstring lower(std::wstring s){for(auto& c:s)c=std::towlower(c);return s;}
std::wstring fileName(const std::wstring&s){auto p=s.find_last_of(L"\\/");return p==std::wstring::npos?s:s.substr(p+1);}
std::wstring imagePath(DWORD pid){HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return L"";wchar_t text[32768];DWORD n=32768;bool ok=QueryFullProcessImageNameW(p,0,text,&n);CloseHandle(p);return ok?std::wstring(text,n):L"";}
DWORD integrity(HANDLE process){HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))return MAXDWORD;DWORD n=0;GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&n);std::vector<BYTE> b(n);DWORD result=MAXDWORD;if(n&&GetTokenInformation(token,TokenIntegrityLevel,b.data(),n,&n)){auto sid=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(b.data())->Label.Sid;auto count=*GetSidSubAuthorityCount(sid);if(count)result=*GetSidSubAuthority(sid,count-1);}CloseHandle(token);return result;}
void setError(std::wstring text){std::lock_guard<std::mutex> g(status.mutex);status.error=std::move(text);}
void writeText(const std::wstring& path,const std::string& text){HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(f,text.data(),static_cast<DWORD>(text.size()),&n,nullptr);CloseHandle(f);}}
void loadConfig(){
 wchar_t app[MAX_PATH];SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,app);
 std::wstring dir=std::wstring(app)+L"\\TouchMouseBridge";CreateDirectoryW(dir.c_str(),nullptr);configPath=dir+L"\\settings.ini";
 auto number=[&](const wchar_t*k,int d){return static_cast<int>(GetPrivateProfileIntW(L"Settings",k,d,configPath.c_str()));};
 config.enabled=number(L"Enabled",0)!=0;config.global=number(L"Global",0)!=0;config.hold=std::clamp(number(L"HoldMs",60),20,250);config.mode=std::clamp(number(L"Clicks",1),1,2);
 HKEY run;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&run)==ERROR_SUCCESS){config.autostart=RegQueryValueExW(run,L"TouchMouseBridge",nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS;RegCloseKey(run);}
 int count=std::min(256,static_cast<int>(GetPrivateProfileIntW(L"Games",L"Count",0,configPath.c_str())));
 for(int i=0;i<count;i++){auto k=L"Path"+std::to_wstring(i);wchar_t path[32768];GetPrivateProfileStringW(L"Games",k.c_str(),L"",path,32768,configPath.c_str());if(*path){auto en=L"Enabled"+std::to_wstring(i);config.games.push_back({path,GetPrivateProfileIntW(L"Games",en.c_str(),1,configPath.c_str())!=0});}}
}
void saveConfig(){
 auto temp=configPath+L".tmp";HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE){setError(L"无法保存设置");return;}WORD bom=0xfeff;DWORD n;WriteFile(f,&bom,2,&n,nullptr);CloseHandle(f);
 auto put=[&](const wchar_t*sec,const wchar_t*key,const std::wstring&value){WritePrivateProfileStringW(sec,key,value.c_str(),temp.c_str());};
 put(L"Settings",L"Enabled",config.enabled?L"1":L"0");put(L"Settings",L"Global",config.global?L"1":L"0");put(L"Settings",L"HoldMs",std::to_wstring(config.hold));put(L"Settings",L"Clicks",std::to_wstring(config.mode));put(L"Games",L"Count",std::to_wstring(config.games.size()));
 for(size_t i=0;i<config.games.size();i++){put(L"Games",(L"Path"+std::to_wstring(i)).c_str(),config.games[i].path);put(L"Games",(L"Enabled"+std::to_wstring(i)).c_str(),config.games[i].enabled?L"1":L"0");}
 WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
 if(!MoveFileExW(temp.c_str(),configPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))setError(L"设置文件保存失败");
}
void applyWorker(){auto* copy=new Config(config);if(!PostThreadMessageW(workerId,M_CONFIG,0,reinterpret_cast<LPARAM>(copy)))delete copy;}
void commit(){saveConfig();applyWorker();updateTray();}
namespace worker {
HWND window=nullptr,foreground=nullptr;
HHOOK mouseHook=nullptr;
HWINEVENTHOOK foregroundHook=nullptr;
Config settings;
DWORD selfIntegrity=0;
bool permitted=false,timer=false,initialHold=false,initialRightHold=false;
bool cancelLeftPending=false,cancelRightPending=false;
std::uintptr_t target=0;
struct InputSink:Sink{
 bool right=false;
 explicit InputSink(bool r=false):right(r){}
 bool valid(std::uintptr_t t,Point p)override{
  HWND w=reinterpret_cast<HWND>(t);if(!IsWindow(w)||GetForegroundWindow()!=w||(testWindow&&w!=testWindow))return false;
  DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&w!=testWindow)return false;
  RECT r;GetClientRect(w,&r);POINT a{r.left,r.top},b{r.right,r.bottom};ClientToScreen(w,&a);ClientToScreen(w,&b);
  return p.x>=a.x&&p.x<b.x&&p.y>=a.y&&p.y<b.y;
 }
 bool move(Point p)override{INPUT i{};i.type=INPUT_MOUSE;i.mi.dx=normalize(p.x,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN));i.mi.dy=normalize(p.y,GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN));i.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;i.mi.dwExtraInfo=OWN_MARK;if(SendInput(1,&i,sizeof i)==1)return true;setError(L"鼠标移动发送失败，请检查目标程序权限");return false;}
 bool button(bool down)override{INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=right?(down?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_RIGHTUP):(down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP);i.mi.dwExtraInfo=OWN_MARK;if(SendInput(1,&i,sizeof i)==1)return true;setError(L"点击发送失败：目标程序可能权限更高");return false;}
} sink,rightSink(true);
Engine engine(sink),rightEngine(rightSink);
bool busy(){return engine.busy()||rightEngine.busy();}
void cancel(){cancelLeftPending=cancelRightPending=false;engine.cancel();rightEngine.cancel();}
void sync(){status.touches=engine.touches+rightEngine.touches;status.clicks=engine.clicks+rightEngine.clicks;status.rightClicks=rightEngine.clicks;status.dropped=engine.dropped+rightEngine.dropped;status.errors=engine.errors+rightEngine.errors;}
void arm(){if(!timer&&busy()){SetTimer(window,1,10,nullptr);timer=true;}}
void updateTarget(){
 HWND now=GetForegroundWindow();if(now!=foreground)cancel();foreground=now;target=reinterpret_cast<std::uintptr_t>(now);permitted=false;
 DWORD pid=0;if(now)GetWindowThreadProcessId(now,&pid);
 std::wstring path=pid?imagePath(pid):L"";
 bool match=settings.global;
 for(const auto& g:settings.games)if(g.enabled&&lower(path)==lower(g.path)){match=true;break;}
 if(testWindow)match=now==testWindow;
 DWORD il=MAXDWORD;HANDLE process=pid?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid):nullptr;
 if(process){il=integrity(process);CloseHandle(process);}
 bool own=pid==GetCurrentProcessId()&&now!=testWindow;
 permitted=settings.enabled&&match&&!own&&il<=selfIntegrity;
 std::wstring text=path.empty()?L"未检测到目标窗口":fileName(path);
 if(settings.enabled&&match&&!own&&il>selfIntegrity)text+=L"（权限高于本工具，保持原触控）";
 {std::lock_guard<std::mutex> g(status.mutex);status.target=text;}
 status.active=permitted;arm();
}
void CALLBACK foregroundChanged(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){updateTarget();}
LRESULT CALLBACK mouseProc(int code,WPARAM message,LPARAM data){
 if(code<0)return CallNextHookEx(mouseHook,code,message,data);
 auto& e=*reinterpret_cast<MSLLHOOKSTRUCT*>(data);
 if(e.dwExtraInfo==OWN_MARK)return CallNextHookEx(mouseHook,code,message,data);
 // Human taps on the self-test surface must not mix with its generated cases.
 if(testWindow&&GetForegroundWindow()==testWindow&&isTouch(e.dwExtraInfo)&&e.dwExtraInfo!=TEST_TOUCH&&sink.valid(reinterpret_cast<std::uintptr_t>(testWindow),{e.pt.x,e.pt.y}))return 1;
 bool swallow=false;
 if(isTouch(e.dwExtraInfo)){
  Point p{e.pt.x,e.pt.y};
  if(message==WM_LBUTTONDOWN){bool eligible=permitted&&foreground==GetForegroundWindow()&&sink.valid(target,p);if(eligible&&rightEngine.busy())cancelRightPending=true;bool pass=false;swallow=engine.nativeTouchDown(target,p,eligible,GetTickCount64(),pass);if(pass){swallow=false;sync();arm();}}
  else if(message==WM_LBUTTONUP){swallow=engine.touchUp(p,GetTickCount64());if(initialHold&&!swallow){engine.physicalUp();initialHold=false;}}
  else if(message==WM_MOUSEMOVE){bool left=engine.touchMove(p),right=rightEngine.touchMove(p);swallow=left||right;}
  else if(message==WM_RBUTTONDOWN){bool eligible=permitted&&foreground==GetForegroundWindow()&&sink.valid(target,p);if(eligible&&engine.busy())cancelLeftPending=true;bool pass=false;swallow=rightEngine.nativeTouchDown(target,p,eligible,GetTickCount64(),pass);if(pass){swallow=false;sync();arm();}}
  else if(message==WM_RBUTTONUP){swallow=rightEngine.touchUp(p,GetTickCount64());if(initialRightHold&&!swallow){rightEngine.physicalUp();initialRightHold=false;}}
  if(testWindow&&GetForegroundWindow()==testWindow&&(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP))testTrace+="touch:"+std::to_string(message)+",extra="+std::to_string(e.dwExtraInfo)+",swallow="+std::to_string(swallow)+",eligible="+std::to_string(permitted)+",match="+std::to_string(foreground==GetForegroundWindow())+",valid="+std::to_string(sink.valid(target,p))+",rightPhysical="+std::to_string(rightEngine.physicalHeld())+",rightBusy="+std::to_string(rightEngine.busy())+",physical="+std::to_string(engine.physicalHeld())+",initial="+std::to_string(initialHold)+",busy="+std::to_string(engine.busy())+",point="+std::to_string(p.x)+"/"+std::to_string(p.y)+"; ";
  if(cancelLeftPending||cancelRightPending){arm();PostMessageW(window,M_WAKE,0,0);}
  if(swallow){sync();arm();PostMessageW(window,M_WAKE,0,0);return 1;}
 }else if(message==WM_LBUTTONDOWN){if(testWindow&&GetForegroundWindow()==testWindow)testTrace+="mouse down extra="+std::to_string(e.dwExtraInfo)+"; ";initialHold=false;engine.physicalDown();sync();}
 else if(message==WM_LBUTTONUP){if(testWindow&&GetForegroundWindow()==testWindow)testTrace+="mouse up extra="+std::to_string(e.dwExtraInfo)+"; ";initialHold=false;engine.physicalUp();}
 else if(message==WM_RBUTTONDOWN){if(testWindow&&GetForegroundWindow()==testWindow)testTrace+="physical R down; ";initialRightHold=false;rightEngine.physicalDown();sync();}
 else if(message==WM_RBUTTONUP){if(testWindow&&GetForegroundWindow()==testWindow)testTrace+="physical R up; ";initialRightHold=false;rightEngine.physicalUp();}
 return CallNextHookEx(mouseHook,code,message,data);
}
void ensureHook(){if(!mouseHook&&(settings.enabled||busy())){mouseHook=SetWindowsHookExW(WH_MOUSE_LL,mouseProc,instance,0);if(!mouseHook)setError(L"无法安装鼠标输入监听，错误 "+std::to_wstring(GetLastError()));}}
void idle(){sync();if(!busy()&&timer){KillTimer(window,1);timer=false;}if(!settings.enabled&&!busy()&&mouseHook){UnhookWindowsHookEx(mouseHook);mouseHook=nullptr;}}
LRESULT CALLBACK proc(HWND hwnd,UINT m,WPARAM w,LPARAM l){
 if(m==WM_TIMER&&w==3){if(GetForegroundWindow()!=foreground){updateTarget();idle();}return 0;}
 if(m==WM_TIMER&&w==2){if(initialHold&&!engine.ownsButton()&&!(GetAsyncKeyState(VK_LBUTTON)&0x8000)){engine.physicalUp();initialHold=false;}if(initialRightHold&&!rightEngine.ownsButton()&&!(GetAsyncKeyState(VK_RBUTTON)&0x8000)){rightEngine.physicalUp();initialRightHold=false;}if(!initialHold&&!initialRightHold)KillTimer(hwnd,2);return 0;}
 if(m==WM_TIMER||m==M_WAKE){
  // SendInput must run after the low-level callback returns, never reenter it
  // while an original down/up batch is still being processed.
  if(cancelLeftPending&&!rightEngine.contactActive()){cancelLeftPending=false;engine.cancel();}if(cancelRightPending&&!engine.contactActive()){cancelRightPending=false;rightEngine.cancel();}
  auto now=GetTickCount64();engine.tick(now);rightEngine.tick(now);idle();return 0;
 }
 if(m==WM_POWERBROADCAST&&w==PBT_APMSUSPEND){cancel();sync();return TRUE;}
 if(m==WM_WTSSESSION_CHANGE&&(w==WTS_SESSION_LOCK||w==WTS_SESSION_LOGOFF)){cancel();sync();return 0;}
 return DefWindowProcW(hwnd,m,w,l);
}
DWORD WINAPI run(void*){
 MSG msg;PeekMessageW(&msg,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
 WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"TouchMouseBridge.Worker.v1";RegisterClassW(&wc);
 window=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
 WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION);
 selfIntegrity=integrity(GetCurrentProcess());foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foregroundChanged,0,0,WINEVENT_OUTOFCONTEXT);
 SetEvent(readyEvent);
 while(GetMessageW(&msg,nullptr,0,0)>0){
  if(!msg.hwnd&&msg.message==M_CONFIG){
   std::unique_ptr<Config> c(reinterpret_cast<Config*>(msg.lParam));bool wasOwned=engine.ownsButton(),wasRightOwned=rightEngine.ownsButton();settings=*c;
   cancelLeftPending=cancelRightPending=false;Settings s;s.enabled=c->enabled;s.holdMs=c->hold;s.clicks=c->mode;engine.configure(s);s.clicks=1;rightEngine.configure(s);
   if(!mouseHook){initialHold=!wasOwned&&(GetAsyncKeyState(VK_LBUTTON)&0x8000);initialRightHold=!wasRightOwned&&(GetAsyncKeyState(VK_RBUTTON)&0x8000);if(initialHold)engine.physicalDown();else engine.physicalUp();if(initialRightHold)rightEngine.physicalDown();else rightEngine.physicalUp();if(initialHold||initialRightHold)SetTimer(window,2,30,nullptr);}
   updateTarget();ensureHook();if(settings.enabled)SetTimer(window,3,250,nullptr);else KillTimer(window,3);idle();++status.revision;
  }
  else if(!msg.hwnd&&msg.message==M_STOP)break;
  else {TranslateMessage(&msg);DispatchMessageW(&msg);}
 }
 cancel();if(mouseHook)UnhookWindowsHookEx(mouseHook);if(foregroundHook)UnhookWinEvent(foregroundHook);WTSUnRegisterSessionNotification(window);DestroyWindow(window);sync();return 0;
}
}
void startWorker(){readyEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);workerHandle=CreateThread(nullptr,0,worker::run,nullptr,0,&workerId);if(workerHandle)WaitForSingleObject(readyEvent,3000);}
void stopWorker(){if(workerHandle){PostThreadMessageW(workerId,M_STOP,0,0);WaitForSingleObject(workerHandle,3000);CloseHandle(workerHandle);workerHandle=nullptr;}if(readyEvent){CloseHandle(readyEvent);readyEvent=nullptr;}}
enum { ID_ENABLE=100,ID_SCOPE,ID_MODE,ID_HOLD,ID_LIST,ID_ADD,ID_REMOVE,ID_HIDE,ID_EXIT,ID_AUTO,ID_HELP,ID_ELEVATE,ID_STATUS,ID_COUNTER,ID_INTRO,ID_SCOPE_LABEL,ID_MODE_LABEL,ID_HOLD_LABEL,ID_GAMES_LABEL,ID_FOOTER,ID_TITLE,ID_SUBTITLE };
int px(int n){return MulDiv(n,dpi,96);}
void font(HWND h,HFONT f){SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(f),TRUE);}
HWND control(const wchar_t* cls,const wchar_t*text,DWORD style,int id,DWORD ex=0){return CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,mainWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);}
void place(int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(mainWindow,id),px(x),px(y),px(w),px(h),TRUE);}
void rebuildList(){
 rebuilding=true;HWND list=GetDlgItem(mainWindow,ID_LIST);ListView_DeleteAllItems(list);
 for(size_t i=0;i<config.games.size();i++){auto name=fileName(config.games[i].path);LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=name.data();ListView_InsertItem(list,&item);ListView_SetItemText(list,item.iItem,1,config.games[i].path.data());ListView_SetCheckState(list,item.iItem,config.games[i].enabled);}
 rebuilding=false;
 EnableWindow(list,!config.global);EnableWindow(GetDlgItem(mainWindow,ID_REMOVE),!config.global);EnableWindow(GetDlgItem(mainWindow,ID_ADD),!config.global);
}
void refreshControls(){
 Button_SetCheck(GetDlgItem(mainWindow,ID_ENABLE),config.enabled?BST_CHECKED:BST_UNCHECKED);
 Button_SetCheck(GetDlgItem(mainWindow,ID_AUTO),config.autostart?BST_CHECKED:BST_UNCHECKED);
 ComboBox_SetCurSel(GetDlgItem(mainWindow,ID_SCOPE),config.global?1:0);ComboBox_SetCurSel(GetDlgItem(mainWindow,ID_MODE),config.mode-1);SetDlgItemInt(mainWindow,ID_HOLD,config.hold,FALSE);
 rebuilding=true;rebuildList();rebuilding=false;
}
void refreshStatus(){
 std::wstring target,error;{std::lock_guard<std::mutex> g(status.mutex);target=status.target;error=status.error;}
 std::wstring state=config.enabled?(status.active?L"正在生效  ·  ":L"已启用，等待目标窗口  ·  "):L"已关闭  ·  ";
 SetDlgItemTextW(mainWindow,ID_STATUS,(state+target).c_str());
 std::wstring stats=L"本次触摸 "+std::to_wstring(status.touches.load())+L" 次   /   鼠标单击 "+std::to_wstring(status.clicks.load())+L" 次";
 stats+=L"\r\n"+(error.empty()?L"长按右键 "+std::to_wstring(status.rightClicks.load())+L" 次":error);
 SetDlgItemTextW(mainWindow,ID_COUNTER,stats.c_str());
}
void layout(){
 RECT r;GetClientRect(mainWindow,&r);int width=MulDiv(r.right,96,dpi),height=MulDiv(r.bottom,96,dpi),w=width-48;
 place(ID_TITLE,24,18,w,30);place(ID_SUBTITLE,24,51,w,22);
 place(ID_ENABLE,24,88,w,32);
 place(ID_SCOPE_LABEL,24,137,92,22);place(ID_SCOPE,126,132,w-102,160);
 place(ID_MODE_LABEL,24,180,92,22);place(ID_MODE,126,175,w-102,160);
 place(ID_HOLD_LABEL,24,223,230,22);place(ID_HOLD,260,218,82,28);place(ID_INTRO,354,224,width-378,22);
 place(ID_GAMES_LABEL,24,268,240,24);place(ID_ADD,width-214,261,91,30);place(ID_REMOVE,width-115,261,91,30);
 int listHeight=std::max(75,height-470);place(ID_LIST,24,299,w,listHeight);
 ListView_SetColumnWidth(GetDlgItem(mainWindow,ID_LIST),0,px(150));ListView_SetColumnWidth(GetDlgItem(mainWindow,ID_LIST),1,px(w-160));
 int below=299+listHeight;
 place(ID_AUTO,24,below+14,w,24);place(ID_STATUS,24,below+48,w,24);place(ID_COUNTER,24,below+77,w,44);
 place(ID_FOOTER,24,height-51,width-166,38);place(ID_ELEVATE,width-130,height-49,106,31);
 for(int id=ID_ENABLE;id<=ID_SUBTITLE;id++)if(auto h=GetDlgItem(mainWindow,id))font(h,id==ID_TITLE?titleFont:normalFont);
 InvalidateRect(mainWindow,nullptr,TRUE);
}
void fonts(){
 if(normalFont)DeleteObject(normalFont);if(titleFont)DeleteObject(titleFont);
 normalFont=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
 titleFont=CreateFontW(-px(25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}
void autoStart(bool enabled){
 HKEY key;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS){setError(L"无法修改开机启动设置");return;}
 LONG r=ERROR_SUCCESS;if(enabled){auto command=L"\""+executable()+L"\" --background";r=RegSetValueExW(key,L"TouchMouseBridge",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));}else{r=RegDeleteValueW(key,L"TouchMouseBridge");if(r==ERROR_FILE_NOT_FOUND)r=ERROR_SUCCESS;}RegCloseKey(key);
 if(r==ERROR_SUCCESS)config.autostart=enabled;else setError(L"开机启动设置保存失败");
 Button_SetCheck(GetDlgItem(mainWindow,ID_AUTO),config.autostart?BST_CHECKED:BST_UNCHECKED);saveConfig();
}
NOTIFYICONDATAW trayData(){
 NOTIFYICONDATAW n{};n.cbSize=sizeof n;n.hWnd=mainWindow;n.uID=1;n.guidItem=TRAY_GUID;
 if(!trayTestOutput.empty())n.guidItem.Data1^=0xf00d;
 n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_GUID|NIF_SHOWTIP;n.uCallbackMessage=M_TRAY;n.hIcon=trayIcon;
 auto tip=std::wstring(L"TouchMouse · 触控兼容助手\n")+(config.enabled?L"已启用触控兼容":L"触控兼容已关闭");
 lstrcpynW(n.szTip,tip.c_str(),static_cast<int>(std::size(n.szTip)));return n;
}
bool addTray(){
 if(!trayIcon){int side=GetSystemMetricsForDpi(SM_CXSMICON,dpi);trayIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,side,side,LR_DEFAULTCOLOR));}
 auto n=trayData();trayAdded=Shell_NotifyIconW(NIM_ADD,&n)!=FALSE;
 if(trayAdded){n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);KillTimer(mainWindow,4);}
 else SetTimer(mainWindow,4,2000,nullptr);
 return trayAdded;
}
void updateTray(){if(trayAdded){auto n=trayData();if(!Shell_NotifyIconW(NIM_MODIFY,&n)){trayAdded=false;SetTimer(mainWindow,4,2000,nullptr);}}}
void removeTray(){if(trayAdded){auto n=trayData();Shell_NotifyIconW(NIM_DELETE,&n);trayAdded=false;}if(trayIcon){DestroyIcon(trayIcon);trayIcon=nullptr;}}
bool hideToTray(HWND hwnd){
 if(!trayAdded&&!addTray()){setError(L"托盘暂不可用，窗口保持打开");ShowWindow(hwnd,SW_RESTORE);return false;}
 KillTimer(hwnd,1);ShowWindow(hwnd,SW_HIDE);return true;
}
void trayMenu(HWND hwnd,POINT point){
 if(point.x==-1&&point.y==-1){NOTIFYICONIDENTIFIER id{};id.cbSize=sizeof id;id.hWnd=hwnd;id.uID=1;id.guidItem=trayData().guidItem;RECT r;if(SUCCEEDED(Shell_NotifyIconGetRect(&id,&r)))point={r.left+(r.right-r.left)/2,r.top};else GetCursorPos(&point);}
 HMENU menu=CreatePopupMenu();if(!menu)return;
 AppendMenuW(menu,MF_STRING,TRAY_SHOW,L"打开设置");SetMenuDefaultItem(menu,TRAY_SHOW,FALSE);
 AppendMenuW(menu,MF_STRING|(config.enabled?MF_CHECKED:MF_UNCHECKED),TRAY_TOGGLE,L"启用触控兼容");
 AppendMenuW(menu,MF_STRING|(config.autostart?MF_CHECKED:MF_UNCHECKED),TRAY_AUTOSTART,L"登录后自动启动");
 AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,TRAY_EXIT,L"退出");
 SetForegroundWindow(hwnd);
 UINT action=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,hwnd,nullptr);
 DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
 if(action==TRAY_SHOW)PostMessageW(hwnd,M_SHOW,0,0);
 else if(action==TRAY_TOGGLE){config.enabled=!config.enabled;Button_SetCheck(GetDlgItem(hwnd,ID_ENABLE),config.enabled?BST_CHECKED:BST_UNCHECKED);commit();refreshStatus();}
 else if(action==TRAY_AUTOSTART)autoStart(!config.autostart);
 else if(action==TRAY_EXIT)DestroyWindow(hwnd);
}
void trayTestStep(HWND hwnd){
 auto check=[&](bool ok,const char* reason){if(!ok&&trayTestFailure.empty())trayTestFailure=reason;};
 if(trayTestPhase==0){
  NOTIFYICONIDENTIFIER id{};id.cbSize=sizeof id;id.hWnd=hwnd;id.uID=1;id.guidItem=trayData().guidItem;RECT r{};
  check(trayAdded&&trayIcon&&SUCCEEDED(Shell_NotifyIconGetRect(&id,&r)),"Tray icon was not registered");
  check(GetDlgItem(hwnd,ID_HIDE)==nullptr&&GetDlgItem(hwnd,ID_EXIT)==nullptr,"Obsolete buttons still exist");
  SendMessageW(hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0);check(!IsWindowVisible(hwnd)&&trayAdded,"Minimize did not hide to tray");
 }else if(trayTestPhase==1){SendMessageW(hwnd,M_SHOW,0,0);check(IsWindowVisible(hwnd),"Could not restore settings");}
 else if(trayTestPhase==2){auto n=trayData();Shell_NotifyIconW(NIM_DELETE,&n);SendMessageW(hwnd,taskbarCreated,0,0);check(trayAdded,"Could not recreate tray icon");}
 else if(trayTestPhase==3){SendMessageW(hwnd,WM_CLOSE,0,0);return;}
 ++trayTestPhase;
}
void addExecutable(){
 wchar_t path[32768]{};OPENFILENAMEW dlg{};dlg.lStructSize=sizeof dlg;dlg.hwndOwner=mainWindow;dlg.lpstrFilter=L"应用程序 (*.exe)\0*.exe\0\0";dlg.lpstrFile=path;dlg.nMaxFile=32768;dlg.lpstrTitle=L"添加需要转换触控的游戏 EXE";dlg.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
 if(GetOpenFileNameW(&dlg)){
  if(lower(path)==lower(executable()))return;
  auto it=std::find_if(config.games.begin(),config.games.end(),[&](const Profile& p){return lower(p.path)==lower(path);});
  if(it==config.games.end()&&config.games.size()<256)config.games.push_back({path,true});else if(it!=config.games.end())it->enabled=true;
  rebuildList();commit();
 }
}
LRESULT CALLBACK mainProc(HWND hwnd,UINT m,WPARAM w,LPARAM l){
 if(taskbarCreated&&m==taskbarCreated){trayAdded=false;addTray();return 0;}
 switch(m){
 case WM_CREATE:{
  mainWindow=hwnd;dpi=GetDpiForWindow(hwnd);fonts();
  control(L"STATIC",L"TouchMouse",SS_LEFT,ID_TITLE);
  control(L"STATIC",L"触控兼容助手  ·  让老游戏识别一次点按",SS_LEFT,ID_SUBTITLE);
  control(L"BUTTON",L"启用触摸 → 鼠标转换",BS_AUTOCHECKBOX|WS_TABSTOP,ID_ENABLE);
  control(L"STATIC",L"生效范围",SS_LEFT,ID_SCOPE_LABEL);auto scope=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,ID_SCOPE);
  ComboBox_AddString(scope,L"仅指定游戏（勾选下方 EXE）");ComboBox_AddString(scope,L"全局（本工具窗口除外）");
  control(L"STATIC",L"转换方式",SS_LEFT,ID_MODE_LABEL);auto mode=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,ID_MODE);
  ComboBox_AddString(mode,L"标准鼠标单击（推荐）");ComboBox_AddString(mode,L"左键双击兼容（长按右键保持单击）");
  control(L"STATIC",L"鼠标按住时长（20–250 毫秒）",SS_LEFT,ID_HOLD_LABEL);
  control(L"EDIT",L"60",ES_NUMBER|ES_CENTER|WS_TABSTOP|WS_BORDER,ID_HOLD);control(L"STATIC",L"默认 60 毫秒",SS_LEFT,ID_INTRO);
  control(L"STATIC",L"游戏列表",SS_LEFT,ID_GAMES_LABEL);control(L"BUTTON",L"添加 EXE…",BS_PUSHBUTTON|WS_TABSTOP,ID_ADD);control(L"BUTTON",L"移除",BS_PUSHBUTTON|WS_TABSTOP,ID_REMOVE);
  auto list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|WS_TABSTOP|WS_BORDER,ID_LIST);
  ListView_SetExtendedListViewStyle(list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
  LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=px(150);col.pszText=const_cast<LPWSTR>(L"游戏程序");ListView_InsertColumn(list,0,&col);col.cx=px(400);col.pszText=const_cast<LPWSTR>(L"完整路径");ListView_InsertColumn(list,1,&col);
  control(L"BUTTON",L"登录 Windows 后自动在后台运行",BS_AUTOCHECKBOX|WS_TABSTOP,ID_AUTO);
  control(L"STATIC",L"",SS_LEFT,ID_STATUS);control(L"STATIC",L"",SS_LEFT,ID_COUNTER);
  control(L"STATIC",L"最小化到托盘 · 关闭窗口退出\r\nCtrl+Alt+F8 开关 · F9 显示 · F12 停用",SS_LEFT,ID_FOOTER);
  control(L"BUTTON",L"管理员运行",BS_PUSHBUTTON|WS_TABSTOP,ID_ELEVATE);
  refreshControls();layout();refreshStatus();SetTimer(hwnd,1,1000,nullptr);
  RegisterHotKey(hwnd,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F8);RegisterHotKey(hwnd,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F9);RegisterHotKey(hwnd,3,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F12);
  DWORD rounded=2;DwmSetWindowAttribute(hwnd,33,&rounded,sizeof rounded);return 0;
 }
 case WM_SIZE:if(w==SIZE_MINIMIZED){PostMessageW(hwnd,M_HIDE,0,0);return 0;}layout();return 0;
 case WM_SYSCOMMAND:if((w&0xfff0)==SC_MINIMIZE){hideToTray(hwnd);return 0;}break;
 case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={px(590),px(590)};return 0;}
 case WM_DPICHANGED:{dpi=HIWORD(w);fonts();auto*r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;}
 case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,RGB(28,35,49));SetBkColor(dc,RGB(249,250,252));return reinterpret_cast<LRESULT>(backgroundBrush);}
 case WM_TIMER:if(w==5){trayTestStep(hwnd);return 0;}if(w==4){if(!trayAdded)addTray();return 0;}refreshStatus();return 0;
 case M_HIDE:hideToTray(hwnd);return 0;
 case M_TRAY:{UINT event=LOWORD(l);if(event==WM_CONTEXTMENU||event==WM_RBUTTONUP)trayMenu(hwnd,{GET_X_LPARAM(w),GET_Y_LPARAM(w)});else if(event==NIN_SELECT||event==NIN_KEYSELECT||event==WM_LBUTTONDBLCLK)PostMessageW(hwnd,M_SHOW,0,0);return 0;}
 case M_SHOW:ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);SetTimer(hwnd,1,1000,nullptr);refreshStatus();return 0;
 case WM_HOTKEY:
  if(w==2){SendMessageW(hwnd,M_SHOW,0,0);return 0;}
  config.enabled=w==3?false:!config.enabled;Button_SetCheck(GetDlgItem(hwnd,ID_ENABLE),config.enabled?BST_CHECKED:BST_UNCHECKED);commit();refreshStatus();return 0;
 case WM_NOTIFY:{auto* h=reinterpret_cast<NMHDR*>(l);if(h->idFrom==ID_LIST&&h->code==LVN_ITEMCHANGED&&!rebuilding){auto*c=reinterpret_cast<NMLISTVIEW*>(l);if(c->iItem>=0&&static_cast<size_t>(c->iItem)<config.games.size()&&((c->uOldState^c->uNewState)&LVIS_STATEIMAGEMASK)){config.games[c->iItem].enabled=ListView_GetCheckState(h->hwndFrom,c->iItem)!=0;commit();}}return 0;}
 case WM_COMMAND:{int id=LOWORD(w),event=HIWORD(w);
  if(id==ID_ENABLE){config.enabled=Button_GetCheck(GetDlgItem(hwnd,id))==BST_CHECKED;commit();refreshStatus();}
  else if(id==ID_SCOPE&&event==CBN_SELCHANGE){config.global=ComboBox_GetCurSel(GetDlgItem(hwnd,id))==1;rebuildList();commit();}
  else if(id==ID_MODE&&event==CBN_SELCHANGE){config.mode=ComboBox_GetCurSel(GetDlgItem(hwnd,id))+1;commit();}
  else if(id==ID_HOLD&&event==EN_KILLFOCUS){BOOL valid;UINT value=GetDlgItemInt(hwnd,id,&valid,FALSE);config.hold=valid?std::clamp(static_cast<int>(value),20,250):60;SetDlgItemInt(hwnd,id,config.hold,FALSE);commit();}
  else if(id==ID_AUTO)autoStart(Button_GetCheck(GetDlgItem(hwnd,id))==BST_CHECKED);
  else if(id==ID_ADD)addExecutable();
  else if(id==ID_REMOVE){int i=ListView_GetNextItem(GetDlgItem(hwnd,ID_LIST),-1,LVNI_SELECTED);if(i>=0&&static_cast<size_t>(i)<config.games.size()){config.games.erase(config.games.begin()+i);rebuildList();commit();}}
  else if(id==ID_ELEVATE){auto exe=executable();auto r=ShellExecuteW(hwnd,L"runas",exe.c_str(),L"--wait-restart",nullptr,SW_SHOWNORMAL);if(reinterpret_cast<INT_PTR>(r)>32)DestroyWindow(hwnd);}
  return 0;
 }
 case WM_CLOSE:DestroyWindow(hwnd);return 0;
 case WM_DESTROY:removeTray();UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);UnregisterHotKey(hwnd,3);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(hwnd,m,w,l);
}
struct TestState { int phase=0,downs=0,ups=0,own=0,native=0,polls=0,rDowns=0,rUps=0,rOwn=0,rNative=0,rPolls=0;bool passed=true;ULONGLONG at=0,focusSince=0;unsigned long long revision=0;std::wstring output;std::string failure,details; } test;
void testSnapshot(){test.details+="stage="+std::to_string(test.phase)+",down="+std::to_string(test.downs)+",up="+std::to_string(test.ups)+",own="+std::to_string(test.own)+",native="+std::to_string(test.native)+",poll="+std::to_string(test.polls)+",rightDown="+std::to_string(test.rDowns)+",rightUp="+std::to_string(test.rUps)+",rightOwn="+std::to_string(test.rOwn)+",rightPoll="+std::to_string(test.rPolls)+",focus="+(GetForegroundWindow()==testWindow?"test":"other")+",dropped="+std::to_string(status.dropped.load())+"; ";}
void testFail(const char*s){test.passed=false;if(test.failure.empty())test.failure=s;}
void resetTestCounts(){test.downs=test.ups=test.own=test.native=test.polls=test.rDowns=test.rUps=test.rOwn=test.rNative=test.rPolls=0;}
void focusTestWindow(){
 SetForegroundWindow(testWindow);if(GetForegroundWindow()==testWindow)return;
 RECT r;GetClientRect(testWindow,&r);POINT p{r.right/2,r.bottom/2};ClientToScreen(testWindow,&p);
 // Only click our own visible test surface, never an obscuring window or lock screen.
 if(WindowFromPoint(p)!=testWindow)return;
 INPUT events[3]{};for(auto& e:events){e.type=INPUT_MOUSE;e.mi.dwExtraInfo=OWN_MARK;}
 events[0].mi.dx=normalize(p.x,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN));events[0].mi.dy=normalize(p.y,GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN));events[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
 events[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;events[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(3,events,sizeof(INPUT));
}
bool injectTest(bool touch,bool down=true,bool up=true,bool right=false){
 if(GetForegroundWindow()!=testWindow){testFail("Test window lost focus; injection cancelled");return false;}
 RECT r;GetClientRect(testWindow,&r);POINT p{r.right/2,r.bottom/2};ClientToScreen(testWindow,&p);
 INPUT items[3]{};items[0].type=INPUT_MOUSE;items[0].mi.dx=normalize(p.x,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN));items[0].mi.dy=normalize(p.y,GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN));items[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;items[0].mi.dwExtraInfo=touch?0xff515780u:0x12345u;
 int n=1;if(down){items[n].type=INPUT_MOUSE;items[n].mi.dwFlags=right?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_LEFTDOWN;items[n++].mi.dwExtraInfo=items[0].mi.dwExtraInfo;}if(up){items[n].type=INPUT_MOUSE;items[n].mi.dwFlags=right?MOUSEEVENTF_RIGHTUP:MOUSEEVENTF_LEFTUP;items[n++].mi.dwExtraInfo=items[0].mi.dwExtraInfo;}
 if(SendInput(n,items,sizeof(INPUT))!=static_cast<UINT>(n)){testFail("Test input could not be submitted");return false;}return true;
}
void testConfigure(bool on,int clicks){config.enabled=on;config.global=false;config.mode=clicks;test.revision=status.revision.load()+1;applyWorker();}
LRESULT CALLBACK testProc(HWND hwnd,UINT m,WPARAM w,LPARAM l){
 if(m==WM_RBUTTONDOWN||m==WM_RBUTTONDBLCLK||m==WM_RBUTTONUP){if(m==WM_RBUTTONUP)++test.rUps;else ++test.rDowns;if(static_cast<ULONG_PTR>(GetMessageExtraInfo())==OWN_MARK)++test.rOwn;else ++test.rNative;return 0;}
 if(m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK||m==WM_LBUTTONUP){if(m==WM_LBUTTONUP)++test.ups;else ++test.downs;if(static_cast<ULONG_PTR>(GetMessageExtraInfo())==OWN_MARK)++test.own;else ++test.native;return 0;}
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);FillRect(dc,&r,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,L"TouchMouse 输入兼容性自测\n约 6 秒，请暂时不要操作此窗口。\n只向本测试窗口发送测试输入。",-1,&r,DT_CENTER|DT_VCENTER|DT_WORDBREAK);EndPaint(hwnd,&ps);return 0;}
 if(m==WM_TIMER){
  auto now=GetTickCount64();if(GetAsyncKeyState(VK_LBUTTON)&0x8000)++test.polls;if(GetAsyncKeyState(VK_RBUTTON)&0x8000)++test.rPolls;
  if(now-test.at>(test.phase==0?90000:5000)){testFail("Test step timed out at stage ");test.failure+=std::to_string(test.phase)+"; worker revision="+std::to_string(status.revision.load())+"; focus="+(GetForegroundWindow()==hwnd?"test":"other");DestroyWindow(hwnd);return 0;}
  if(test.phase==0&&status.revision.load()>=test.revision){if(GetForegroundWindow()!=hwnd){test.focusSince=0;focusTestWindow();return 0;}if(GetAsyncKeyState(VK_LBUTTON)&0x8000){test.focusSince=0;return 0;}if(!test.focusSince){test.focusSince=now;return 0;}if(now-test.focusSince<650)return 0;resetTestCounts();injectTest(true);test.at=now;test.phase=1;}
  else if(test.phase==1&&now-test.at>400){testSnapshot();if(test.downs!=1||test.ups!=1||test.own!=1||test.native!=1||test.polls<1||(GetAsyncKeyState(VK_LBUTTON)&0x8000))testFail("Single-click conversion/polling/release failed");resetTestCounts();testConfigure(true,2);test.phase=2;test.at=now;}
  else if(test.phase==2&&status.revision.load()>=test.revision){injectTest(true);test.at=now;test.phase=3;}
  else if(test.phase==3&&now-test.at>550){testSnapshot();if(test.downs!=2||test.ups!=2||test.own!=3||test.native!=1||test.polls<1)testFail("Double-click conversion failed");resetTestCounts();testConfigure(true,1);test.phase=4;test.at=now;}
  else if(test.phase==4&&status.revision.load()>=test.revision){injectTest(false);test.phase=5;test.at=now;}
  else if(test.phase==5&&now-test.at>200){testSnapshot();if(test.downs!=1||test.ups!=1||test.own!=0)testFail("Non-touch mouse input was changed");resetTestCounts();injectTest(true,true,false);test.phase=6;test.at=now;}
  else if(test.phase==6&&now-test.at>120){testSnapshot();if(test.downs!=1||!test.polls)testFail("Held contact did not produce a down state");testConfigure(false,1);test.phase=7;test.at=now;}
  else if(test.phase==7&&status.revision.load()>=test.revision&&now-test.at>100){injectTest(true,false,true);test.phase=8;test.at=now;}
  else if(test.phase==8&&now-test.at>200){testSnapshot();if(test.ups!=1||(GetAsyncKeyState(VK_LBUTTON)&0x8000))testFail("Disabling conversion did not safely release the button");resetTestCounts();testConfigure(true,2);test.phase=9;test.at=now;}
  else if(test.phase==9&&status.revision.load()>=test.revision){injectTest(true,true,true,true);test.phase=10;test.at=now;}
  else if(test.phase==10&&now-test.at>400){testSnapshot();if(test.rDowns!=1||test.rUps!=1||test.rOwn!=1||test.rNative!=1||!test.rPolls||test.downs||test.ups||(GetAsyncKeyState(VK_RBUTTON)&0x8000))testFail("Touch right click was missed or repeated in double-click mode");resetTestCounts();injectTest(false,true,true,true);test.phase=11;test.at=now;}
  else if(test.phase==11&&now-test.at>200){testSnapshot();if(test.rDowns!=1||test.rUps!=1||test.rOwn!=0)testFail("Physical right click was modified");resetTestCounts();injectTest(true,true,false);test.phase=12;test.at=now;}
  else if(test.phase==12&&now-test.at>120){if(test.downs!=1||!test.polls)testFail("Tentative left down was not delivered");injectTest(true,true,true,true);test.phase=13;test.at=now;}
  else if(test.phase==13&&now-test.at>400){testSnapshot();if(test.ups!=1||test.rDowns!=1||test.rUps!=1||!test.rPolls||(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000))testFail("Right gesture did not release a tentative touch left press");resetTestCounts();injectTest(true,true,false,true);test.phase=14;test.at=now;}
  else if(test.phase==14&&now-test.at>120){if(test.rDowns!=1||!test.rPolls)testFail("Held right contact was not delivered");testConfigure(false,1);test.phase=15;test.at=now;}
  else if(test.phase==15&&status.revision.load()>=test.revision&&now-test.at>100){injectTest(true,false,true,true);test.phase=16;test.at=now;}
  else if(test.phase==16&&now-test.at>200){testSnapshot();if(test.rUps!=1||(GetAsyncKeyState(VK_RBUTTON)&0x8000))testFail("Disabling conversion did not release right button");DestroyWindow(hwnd);}
  return 0;
 }
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(hwnd,m,w,l);
}
int selfTest(const std::wstring& output){
 test.output=output;WNDCLASSW wc{};wc.lpfnWndProc=testProc;wc.hInstance=instance;wc.lpszClassName=L"TouchMouseBridge.SelfTest";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
 testWindow=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"TouchMouse compatibility self-test",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,80,80,560,320,nullptr,nullptr,instance,nullptr);ShowWindow(testWindow,SW_SHOW);SetForegroundWindow(testWindow);
 startWorker();testConfigure(true,1);test.at=GetTickCount64();SetTimer(testWindow,1,15,nullptr);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}stopWorker();
 std::string report="{\n  \"passed\": "+std::string(test.passed?"true":"false")+",\n  \"tests\": [\"touch single click\", \"60ms held state observable by polling\", \"double-click mode\", \"physical mouse passthrough\", \"safe release on disable\", \"touch right click held for polling\", \"right click stays single in double mode\", \"physical right mouse passthrough\", \"right gesture cancels touch left hold\", \"safe right release on disable\"],\n  \"touches\": "+std::to_string(status.touches.load())+",\n  \"clicks\": "+std::to_string(status.clicks.load())+",\n  \"errors\": "+std::to_string(status.errors.load())+",\n  \"failure\": \""+test.failure+"\",\n  \"details\": \""+test.details+"\",\n  \"trace\": \""+testTrace+"\",\n  \"scope\": \"synthetic signature events in the application-owned test window; real touchscreen still requires device validation\"\n}\n";
 writeText(output,report);return test.passed?0:1;
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
 instance=h;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 INITCOMMONCONTROLSEX controls{sizeof controls,ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
 int argc;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool waiting=false,quit=false;std::wstring testOutput;
 for(int i=1;i<argc;i++){if(wcscmp(argv[i],L"--background")==0)backgroundStart=true;else if(wcscmp(argv[i],L"--quit")==0)quit=true;else if(wcscmp(argv[i],L"--wait-restart")==0)waiting=true;else if(wcscmp(argv[i],L"--self-test")==0&&i+1<argc)testOutput=argv[++i];else if(wcscmp(argv[i],L"--tray-self-test")==0&&i+1<argc)trayTestOutput=argv[++i];}
 LocalFree(argv);if(!testOutput.empty())return selfTest(testOutput);
 if(quit){auto old=FindWindowW(MAIN_CLASS,nullptr);return old&&PostMessageW(old,WM_CLOSE,0,0)?0:1;}
 HANDLE mutex=CreateMutexW(nullptr,TRUE,trayTestOutput.empty()?L"Local\\TouchMouseBridge.v1":L"Local\\TouchMouseBridge.TrayTest");if(!mutex)return 1;
 if(GetLastError()==ERROR_ALREADY_EXISTS){if(waiting){DWORD r=WaitForSingleObject(mutex,6000);if(r!=WAIT_OBJECT_0&&r!=WAIT_ABANDONED){CloseHandle(mutex);return 1;}}else{auto old=FindWindowW(MAIN_CLASS,nullptr);if(old)PostMessageW(old,M_SHOW,0,0);CloseHandle(mutex);return 0;}}
 loadConfig();if(!trayTestOutput.empty())config.enabled=false;backgroundBrush=CreateSolidBrush(RGB(249,250,252));taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
 WNDCLASSEXW wc{};wc.cbSize=sizeof wc;wc.hInstance=h;wc.lpszClassName=MAIN_CLASS;wc.lpfnWndProc=mainProc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(h,MAKEINTRESOURCEW(1));if(!wc.hIcon)wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hIconSm=wc.hIcon;wc.hbrBackground=backgroundBrush;RegisterClassExW(&wc);
 // Initial sizing uses the primary monitor scale; WM_DPICHANGED handles moves later.
 dpi=GetDpiForSystem();
 mainWindow=CreateWindowExW(0,MAIN_CLASS,L"TouchMouse · 触控兼容助手",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,px(650),px(665),nullptr,nullptr,h,nullptr);
 if(!mainWindow){ReleaseMutex(mutex);CloseHandle(mutex);return 1;}
 ChangeWindowMessageFilterEx(mainWindow,M_SHOW,MSGFLT_ALLOW,nullptr);
 if(taskbarCreated)ChangeWindowMessageFilterEx(mainWindow,taskbarCreated,MSGFLT_ALLOW,nullptr);
 startWorker();applyWorker();
 addTray();
 if(backgroundStart&&trayAdded){KillTimer(mainWindow,1);ShowWindow(mainWindow,SW_HIDE);}else{ShowWindow(mainWindow,SW_SHOW);UpdateWindow(mainWindow);}
 if(!trayTestOutput.empty())SetTimer(mainWindow,5,350,nullptr);
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(mainWindow,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 stopWorker();if(normalFont)DeleteObject(normalFont);if(titleFont)DeleteObject(titleFont);DeleteObject(backgroundBrush);ReleaseMutex(mutex);CloseHandle(mutex);
 if(!trayTestOutput.empty()){if(trayAdded&&trayTestFailure.empty())trayTestFailure="Tray icon remained after close";writeText(trayTestOutput,"{\"passed\":"+std::string(trayTestFailure.empty()?"true":"false")+",\"failure\":\""+trayTestFailure+"\",\"checks\":[\"icon registered\",\"obsolete buttons removed\",\"minimize hides to tray\",\"restore settings\",\"Explorer recreation callback\",\"close exits and removes icon\"]}\n");return trayTestFailure.empty()?0:1;}
 return 0;
}
