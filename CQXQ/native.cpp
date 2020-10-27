#include <vector>
#include <string>
#include <Windows.h>
#include <thread>
#include <future>
#include <filesystem>
#include <fstream>
#include <set>
#include <exception>
#include <stdexcept>
#include <sstream>
#include "GlobalVar.h"
#include "native.h"
#include "CQTools.h"
#include "nlohmann/json.hpp"
#include "ctpl_stl.h"
#include "Unpack.h"
#include "GlobalVar.h"
#include "GUI.h"
#include "resource.h"
#include "EncodingConvert.h"
#include <regex>
#include "RichMessage.h"
#include "ErrorHandler.h"
#include "CQPluginLoader.h"
#include <CommCtrl.h>
#include <DbgHelp.h>
#include <combaseapi.h>

#pragma comment(lib, "urlmon.lib")

// 包含一次实现
#define XQAPI_IMPLEMENTATION
#include "XQAPI.h"

using namespace std;

#define XQ

class Cominit
{
public:
	HRESULT hr;
	Cominit()
	{
		hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	}
	~Cominit()
	{
		if (SUCCEEDED(hr)) CoUninitialize();
	}
};

HMODULE XQHModule = nullptr;
HMODULE CQPHModule = nullptr;

// 获取CQ码某部分的信息
std::string retrieveSectionData(const std::string& CQCode, const std::string& section)
{
	string ret;
	std::string sectionFull = section + "=";
	size_t Loc = CQCode.find(sectionFull);
	if (Loc != string::npos)
	{
		ret = CQCode.substr(Loc + sectionFull.size(), CQCode.find(',', Loc + sectionFull.size()) - Loc - sectionFull.size());
	}
	return ret;
}


// XQ码到CQ码
std::string parseToCQCode(const char* msg)
{
	if (!msg) return "";
	std::string_view msgStr(msg);
	std::string ret;
	size_t l = 0, r = 0, last = 0;
	l = msgStr.find("[");
	r = msgStr.find("]", l);
	while (l != string::npos && r != string::npos)
	{
		ret += msgStr.substr(last, l - last);
		if (msgStr.substr(l, 2) == "[@")
		{
			ret += "[CQ:at,qq=";
			ret += msgStr.substr(l + 2, r - l - 2);
			ret += "]";
		}
		else if (msgStr.substr(l, 5) == "[pic=")
		{
			size_t commaLoc = msgStr.find(',', l);
			ret += "[CQ:image,file=";
			if (commaLoc < r)
			{
				ret += msgStr.substr(l + 5, commaLoc - l - 5);
			}
			else
			{
				ret += msgStr.substr(l + 5, r - l - 5);
			}
			ret += "]";
		}
		else if (msgStr.substr(l, 5) == "[Voi=")
		{
			ret += "[CQ:record,file=";
			ret += msgStr.substr(l + 5, r - l - 5);
			ret += "]";
		}
		else if (msgStr.substr(l, 5) == "[Face")
		{
			ret += "[CQ:face,id=";
			ret += msgStr.substr(l + 5, r - l - 5 - 4);
			ret += "]";
		}
		else
		{
			ret += msgStr.substr(l, r - l + 1);
		}
		last = r + 1;
		l = msgStr.find("[", r);
		r = msgStr.find(']', l);
	}
	ret += msgStr.substr(last);
	return ret;
}


// CQ码到XQ码
std::string parseCQCodeAndSend(int32_t msgType, const char* targetId, const char* QQ, const std::string& msg, int32_t bubbleID, BOOL isAnon, BOOL AnonIgnore, const char* json)
{
	if (msg.empty()) return "";
	std::string_view msgStr(msg);
	std::string ret;
	size_t l = 0, r = 0, last = 0;
	l = msgStr.find("[CQ");
	r = msgStr.find("]", l);
	while (l != string::npos && r != string::npos)
	{
		ret += msgStr.substr(last, l - last);
		if (msgStr.substr(l, 10) == "[CQ:at,qq=")
		{
			ret += "[@";
			ret += msgStr.substr(l + 10, r - l - 10);
			ret += "]";
		}
		else if (msgStr.substr(l, 10) == "[CQ:image,")
		{
			
			std::string imageStr;
			imageStr += msgStr.substr(l + 10, r - l - 10);
			// 现在的状态是file=xxx.jpg/png/gif/...(,cache=xxx)
			std::string fileStr;
			fileStr = retrieveSectionData(imageStr, "file");
			if (fileStr.empty())
			{
				fileStr = retrieveSectionData(imageStr, "url");
			}

			// 判断是已有图片，本地图片，网络URL还是Base64
			if (!fileStr.empty())
			{
				// 已有图片
				if (fileStr[0] == '{')
				{
					regex groupPic("\\{([0-9A-Fa-f]{8})[-]([0-9A-Fa-f]{4})[-]([0-9A-Fa-f]{4})[-]([0-9A-Fa-f]{4})[-]([0-9A-Fa-f]{12})\\}\\.(jpg|png|gif|bmp|jpeg).*", regex::ECMAScript | regex::icase);
					regex privatePic("\\{[0-9]{5,15}[-][0-9]{5,15}[-]([0-9A-Fa-f]{32})\\}\\.(jpg|png|gif|bmp|jpeg).*", regex::ECMAScript | regex::icase);
					smatch m;
					// 转换群聊和好友图片
					if ((msgType == 1 || msgType == 4 || msgType == 5) && regex_match(fileStr, m, groupPic))
					{
						fileStr = "{"s + to_string(robotQQ).c_str() + "-" + "1234567879" + "-" + m[1].str() + m[2].str() + m[3].str() + m[4].str() + m[5].str() + "}" + "." + m[6].str();
					}
					else if ((msgType == 2 || msgType == 3)&& regex_match(fileStr, m, privatePic))
					{
						std::string guid = m[1].str();
						fileStr = "{"s + guid.substr(0, 8) + "-" + guid.substr(8, 4) + "-" + guid.substr(12, 4) + "-" + guid.substr(16, 4) + "-" + guid.substr(20) + "}" + "." + m[2].str();
					}
					ret += "[pic=";
					ret += fileStr;
					ret += "]";
				}
				else if (fileStr.substr(0, 4) == "http" || fileStr.substr(0, 3) == "ftp" || fileStr.substr(0, 2) == "ww")
				{
					ret += "[pic=";
					ret += fileStr;
					ret += "]";
				}
				else if (fileStr.substr(0, 6) == "base64")
				{
					// 格式应该是base64://...
					std::string imageData = base64_decode(fileStr.substr(9));
					int header = 1, length = imageData.size();
					imageData = std::string(reinterpret_cast<char*>(&header), 4) + std::string(reinterpret_cast<char*>(&length), 4) + imageData;
					const char* pic = XQAPI::UpLoadPic(to_string(robotQQ).c_str(), (msgType == 2 || msgType == 3) ? 2 : 1, targetId, imageData.c_str() + 8);
					if (!pic || strlen(pic) == 0)
					{
						ret += "空图片";
					}
					else
					{
						ret += pic;
					}
				}
				else
				{
					// file:///...
					if (fileStr.substr(0, 4) == "file")
					{
						fileStr = fileStr.substr(8);
					}
					else
					{
						fileStr = rootPath + "\\data\\image\\" + fileStr;
					}
					ret += "[pic=";
					ret += fileStr;
					ret += "]";
				}
			}
		}
		else if (msgStr.substr(l, 16) == "[CQ:record,file=")
		{
			if (msgStr[l + 16] == '{')
			{
				ret += "[Voi=";
				ret += msgStr.substr(l + 16, r - l - 16);
				ret += "]";
			}
			else
			{
				std::string ppath = rootPath + "\\data\\record\\";
				ppath += msgStr.substr(l + 16, r - l - 16);
				ret += "[Voi=";
				ret += ppath;
				ret += "]";
			}
		}
		else if (msgStr.substr(l, 12) == "[CQ:face,id=")
		{
			ret += "[Face";
			ret += msgStr.substr(l + 12, r - l - 12);
			ret += ".gif]";
		}
		else if (msgStr.substr(l, 13) == "[CQ:emoji,id=")
		{
			try
			{
				std::string idStr(msgStr.substr(l + 13, r - l - 13));
				u32string u32_str;
				if (idStr.substr(0, 6) == "100000")
				{
					u32_str.append({ static_cast<char32_t>(std::stoul(idStr.substr(6))), 0xFE0F, 0x20E3 });
				}
				else
				{
					u32_str.append({ static_cast<char32_t>(std::stoul(idStr)) });
				}
				
				std::string utf8 = ConvertEncoding<char>(u32_str, "utf-32le", "utf-8");
				std::stringstream stream;
				for (char c : utf8)
				{
					stream << setfill('0') << hex << uppercase << setw(2) << (0xff & (unsigned int)c);
				}
				ret += "[emoji=";
				ret += stream.str();
				ret += "]";
			}
			catch (...)
			{
				ret += msgStr.substr(l, r - l + 1);
			}
		}
		else if (msgStr.substr(l, 10) == "[CQ:share,")
		{
			std::string shareStr;
			shareStr += msgStr.substr(l + 10, r - l - 10);
			//title
			string title = retrieveSectionData(shareStr, "title");
			//url
			string url = retrieveSectionData(shareStr, "url");
			//content
			string content = retrieveSectionData(shareStr, "content");
			//image
			string image = retrieveSectionData(shareStr, "image");

			XQAPI::SendXML(std::to_string(robotQQ).c_str(), 0, msgType, targetId, QQ, constructXMLShareMsg(url, title, content, image).c_str(), 0);
		}
		else if (msgType == 1 && msgStr.substr(l, 9) == "[CQ:shake")
		{
			//好友&抖动
			XQAPI::ShakeWindow(std::to_string(robotQQ).c_str(), QQ);
		}
		else
		{
			ret += msgStr.substr(l, r - l + 1);
		}
		last = r + 1;
		l = msgStr.find("[CQ", r);
		r = msgStr.find(']', l);
	}
	ret += msgStr.substr(last);
	if (!ret.empty())
	{
		const char* rret;
		if (msgType == 2 && isAnon && AnonIgnore)
		{
			if (XQAPI::GetAnon(std::to_string(robotQQ).c_str(), targetId))
			{
				// 尝试发送
				rret = XQAPI::SendMsgEX_V2(std::to_string(robotQQ).c_str(), msgType, targetId, QQ, ret.c_str(), bubbleID, isAnon, json);
				try
				{
					if (!rret || strcmp(rret, "") == 0)
					{
						throw std::exception();
					}
					nlohmann::json j = nlohmann::json::parse(rret);
					if (!j["sendok"].get<bool>())
					{
						throw std::exception();
					}
				}
				catch (std::exception&)
				{
					rret = XQAPI::SendMsgEX_V2(std::to_string(robotQQ).c_str(), msgType, targetId, QQ, ret.c_str(), bubbleID, FALSE, json);
				}
			}
			else
			{
				// 没开启匿名
				rret = XQAPI::SendMsgEX_V2(std::to_string(robotQQ).c_str(), msgType, targetId, QQ, ret.c_str(), bubbleID, FALSE, json);
			}
		}
		else
		{
			rret = XQAPI::SendMsgEX_V2(std::to_string(robotQQ).c_str(), msgType, targetId, QQ, ret.c_str(), bubbleID, isAnon, json);
		}
		
		return rret ? rret : "";
	}
	// 最开始非空但是解析CQ码以后为空说明其中的内容（卡片之类的）在前面被发送出去了，这个时候不应该返回失败，强制返回成功
	return "FORCESUC";
}

std::string nickToCQCode(const std::string& msg)
{
	std::string ret;
	std::regex match("(&nbsp;|\\[em\\](e[0-9]{1,6})\\[\\/em\\])", std::regex::ECMAScript | std::regex::icase);
	std::smatch m;
	int last = 0;
	while (regex_search(msg.begin() + last, msg.end(), m, match)) 
	{
		ret.append(m.prefix());
		if (m[0].str()[0] == '&')
		{
			ret.append(" ");
		} 
		else
		{
			int codepoint = std::stoi(m[2].str().substr(1));
			if (codepoint > 200000)
			{
				u32string u32_str;
				u32_str.append({ static_cast<char32_t>(codepoint - 200000) });

				std::string utf8 = ConvertEncoding<char>(u32_str, "utf-32le", "utf-8");
				std::stringstream stream;
				for (char c : utf8)
				{
					stream << setfill('0') << hex << uppercase << setw(2) << (0xff & (unsigned int)c);
				}
				ret += "[emoji=";
				ret += stream.str();
				ret += "]";
			}
			else if (codepoint >= 100000)
			{
				ret.append("[Face" + std::to_string(codepoint - 100000) + ".gif]");
			}
			else
			{
				ret.append("[CQ:image,url=http://qzonestyle.gtimg.cn/qzone/em/" + m[2].str() + ".gif]");
			}
		}
		last += m.position() + m.length();
	}
	ret.append(msg.substr(last));
	return ret;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		hDllModule = hModule;
		break;
	}
	
	return TRUE;
}




void __stdcall MsgLoop()
{
	MSG msg{};
	while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	this_thread::sleep_for(5ms);
	if (running) fakeMainThread.push([](int) {ExceptionWrapper(MsgLoop)(); });
}

void __stdcall CQXQ_init()
{
	// 获取文件目录
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string pathStr(path);
	rootPath = pathStr.substr(0, pathStr.rfind("\\"));

	running = true;

	// 初始化伪主线程
	fakeMainThread.push([](int) { 
		HRESULT hr = OleInitialize(nullptr);
		INITCOMMONCONTROLSEX ex;
		ex.dwSize = sizeof(ex);
		ex.dwICC = ICC_ANIMATE_CLASS | ICC_BAR_CLASSES | ICC_COOL_CLASSES | ICC_DATE_CLASSES | ICC_HOTKEY_CLASS | ICC_INTERNET_CLASSES |
			ICC_LINK_CLASS | ICC_LISTVIEW_CLASSES | ICC_NATIVEFNTCTL_CLASS | ICC_PAGESCROLLER_CLASS | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES |
			ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_USEREX_CLASSES;
		InitCommonControlsEx(&ex);
		ExceptionWrapper(InitGUI)();
	}).wait();

	fakeMainThread.push([](int) { ExceptionWrapper(MsgLoop)(); });

	// 载入配置
	filesystem::path p(rootPath);
	p.append("CQPlugins").append(".cqxq_recv_self_event.enable");
	RecvSelfEvent = filesystem::exists(p);

	// 载入API DLL并加载API函数
#ifdef XQ
	XQHModule = LoadLibraryA("xqapi.dll");
#else
	XQHModule = LoadLibraryA("OQapi.dll");
#endif
	XQAPI::initFuncs(XQHModule);

	// 写出CQP.dll
	HRSRC rscInfo = FindResourceA(hDllModule, MAKEINTRESOURCEA(IDR_CQP1), "CQP");
	HGLOBAL rsc = nullptr;
	char* rscPtr = nullptr; 
	DWORD size = 0;
	if (rscInfo)
	{
		rsc = LoadResource(hDllModule, rscInfo);
		size = SizeofResource(hDllModule, rscInfo);
		if (rsc) rscPtr = (char*)LockResource(rsc);
	}

	if (rscPtr && size)
	{
		std::string rscStr(rscPtr, size);
		ofstream ofCQP(rootPath + "\\CQP.dll", ios::out | ios::trunc | ios::binary);
		if (ofCQP)
		{
			ofCQP << rscStr;
			XQAPI::OutPutLog("写出CQP.dll成功");
		}
		else
		{
			XQAPI::OutPutLog("写出CQP.dll失败！无法创建文件输出流！");
		}
	}
	else
	{
		XQAPI::OutPutLog("写出CQP.dll失败！获取资源失败！");
	}

	// 加载CQP.dll
	CQPHModule = LoadLibraryA("CQP.dll");

	// 创建必要文件夹
	std::filesystem::create_directories(rootPath + "\\data\\image\\");
	std::filesystem::create_directories(rootPath + "\\data\\record\\");
	std::filesystem::remove_all(rootPath + "\\CQPlugins\\tmp\\");
	std::filesystem::create_directories(rootPath + "\\CQPlugins\\tmp\\");

	// 加载CQ插件
	loadAllCQPlugin();

	// 延迟字符串内存释放
	memFreeThread = std::make_unique<std::thread>([]
	{
		while (running)
		{
			{
				std::unique_lock lock(memFreeMutex);
				// 延迟5分钟释放字符串内存
				while (!memFreeQueue.empty() && time(nullptr) - memFreeQueue.top().first > 300)
				{
					free((void*)memFreeQueue.top().second);
					memFreeQueue.pop();
				}
			}
			{
				std::unique_lock lock(memFreeMsgIdMutex);
				// 延迟5分钟释放MsgId内存
				while (!memFreeMsgIdQueue.empty() && time(nullptr) - memFreeMsgIdQueue.top().first > 300)
				{
					if (msgIdMap.count(memFreeMsgIdQueue.top().second))
					{
						msgIdMap.erase(memFreeMsgIdQueue.top().second);
					}
					memFreeMsgIdQueue.pop();
				}
			}
			std::this_thread::sleep_for(1s);
		}
		// 在线程退出时释放掉所有内存
		std::unique_lock lock(memFreeMutex);
		while (!memFreeQueue.empty())
		{
			free((void*)memFreeQueue.top().second);
			memFreeQueue.pop();
		}
	});
	Init = true;
}

#ifdef XQ
CQAPI(void, XQ_AuthId, 8)(int ID, int IMAddr)
{
	AuthCode = new unsigned char[16];
	*((int*)AuthCode) = 1;
	*((int*)(AuthCode + 4)) = 8;
	*((int*)(AuthCode + 8)) = ID;
	*((int*)(AuthCode + 12)) = IMAddr;
	AuthCode += 8;
}
CQAPI(void, XQ_AutoId, 8)(int ID, int IMAddr)
{
	AuthCode = new unsigned char[16];
	*((int*)AuthCode) = 1;
	*((int*)(AuthCode + 4)) = 8;
	*((int*)(AuthCode + 8)) = ID;
	*((int*)(AuthCode + 12)) = IMAddr;
	AuthCode += 8;
}
#endif

#ifdef XQ
CQAPI(const char*, XQ_Create, 4)(const char* ver)
#else
CQAPI(const char*, OQ_Create, 0)()
#endif
{
#ifdef XQ
	return "{\"name\":\"CQXQ\", \"pver\":\"1.1.0beta\", \"sver\":3, \"author\":\"Suhui\", \"desc\":\"A simple compatibility layer between CQ and XQ\"}";
#else
	return "插件名称{CQOQ}\r\n插件版本{1.1.0beta}\r\n插件作者{Suhui}\r\n插件说明{A simple compatibility layer between CQ and OQ}\r\n插件skey{8956RTEWDFG3216598WERDF3}\r\n插件sdk{S3}";
#endif
}

void __stdcall CQXQ_Uninit()
{
	for (auto& plugin : plugins)
	{
		FreeLibrary(plugin.second.dll);
	}
	filesystem::remove_all(rootPath + "\\CQPlugins\\tmp\\");
	FreeLibrary(XQHModule);
	FreeLibrary(CQPHModule);
	running = false;
	memFreeThread->join();
	memFreeThread.reset(nullptr);
	fakeMainThread.push([](int) {
		DestroyMainWindow();
		int bRet;
		MSG msg{};
		while ((bRet = GetMessageA(&msg, nullptr, 0, 0)) != 0)
		{
			if (bRet == -1)
			{
				break;
			}
			else
			{
				TranslateMessage(&msg);
				DispatchMessageA(&msg);
			}
		}
		OleUninitialize();
	}).wait();
	fakeMainThread.stop();
	p.stop();
	if (AuthCode)
	{
		AuthCode -= 8;
		delete[] AuthCode;
	}
}

#ifdef XQ
CQAPI(int32_t, XQ_DestroyPlugin, 0)()
#else
CQAPI(int32_t, OQ_DestroyPlugin, 0)()
#endif
{
	ExceptionWrapper(CQXQ_Uninit)();
	return 0;
}

#ifdef XQ
CQAPI(int32_t, XQ_SetUp, 0)()
#else
CQAPI(int32_t, OQ_SetUp, 0)()
#endif
{
	ExceptionWrapper(ShowMainWindow)();
	return 0;
}

// QQ-群号 缓存 用于发送消息
std::map<int64_t, int64_t> UserGroupCache;

// QQ-讨论组号 缓存 用于发送消息
std::map<int64_t, int64_t> UserDiscussCache;

// 群-群成员json字符串缓存 用于获取群成员列表，群成员信息，缓存时间1小时，遇到群成员变动事件/群名片更改事件刷新
std::map<long long, std::pair<std::string, time_t>> GroupMemberCache;

// 群列表缓存 用于获取群列表，缓存时间1小时，遇到群添加/退出等事件刷新
std::pair<std::string, time_t> GroupListCache;

int __stdcall CQXQ_process(const char* botQQ, int32_t msgType, int32_t subType, const char* sourceId, const char* activeQQ, const char* passiveQQ, const char* msg, const char* msgNum, const char* msgId, const char* rawMsg, const char* timeStamp, char* retText)
{
	botQQ = botQQ ? botQQ : "";
	sourceId = sourceId ? sourceId : "";
	activeQQ = activeQQ ? activeQQ : "";
	passiveQQ = passiveQQ ? passiveQQ : "";
	msg = msg ? msg : "";
	msgNum = msgNum ? msgNum : "";
	msgId = msgId ? msgId : "";
	rawMsg = rawMsg ? rawMsg : "";
	timeStamp = timeStamp ? timeStamp : "";

	std::string botQQStr = botQQ;

	if (robotQQ == 0) robotQQ = atoll(botQQ);
	if (!botQQStr.empty() && robotQQ != atoll(botQQ)) return 0;
	if (msgType == XQ_Load)
	{
		p.push([](int) { ExceptionWrapper(CQXQ_init)(); });
		return 0;
	}
	while (!Init)
	{
		this_thread::sleep_for(100ms);
	}
	if (msgType == XQ_Exit)
	{
		for (const auto& plugin : plugins_events[CQ_eventExit])
		{
			const auto exit = IntMethod(plugin.event);
			if (exit)
			{
				fakeMainThread.push([&exit](int) { ExceptionWrapper(exit)(); }).wait();
			}
		}
		return 0;
	}
	if (msgType == XQ_Enable)
	{
		fakeMainThread.push([](int)
		{
			this_thread::sleep_for(1s);
			const char* onlineList = XQAPI::GetOnLineList();
			std::string onlineListStr = onlineList ? onlineList : "";

			if (!onlineListStr.empty() && !(onlineListStr[0] == '\r' || onlineListStr[0] == '\n') && !EnabledEventCalled)
			{
				if (robotQQ == 0)
				{
					robotQQ = atoll(onlineList);
				}
				// 先返回此函数，让先驱认为插件已经开启再调用插件的启用函数

				for (const auto& plugin : plugins_events[CQ_eventEnable])
				{
					if (!plugins[plugin.plugin_id].enabled) continue;
					const auto enable = IntMethod(plugin.event);
					if (enable)
					{
						ExceptionWrapper(enable)();
					}
				}
				EnabledEventCalled = true;
			}
		});
	
		return 0;
	}
	if (msgType == XQ_LogInComplete)
	{
		fakeMainThread.push([](int) {
			if (!EnabledEventCalled && XQAPI::IsEnable())
			{
				for (const auto& plugin : plugins_events[CQ_eventEnable])
				{
					if (!plugins[plugin.plugin_id].enabled) continue;
					const auto enable = IntMethod(plugin.event);
					if (enable)
					{
						ExceptionWrapper(enable)();
					}
				}
				EnabledEventCalled = true;
			}
		});
		return 0;
	}
	if (msgType == XQ_Disable)
	{
		if (!EnabledEventCalled) return 0;
		EnabledEventCalled = false;
		for (const auto& plugin : plugins_events[CQ_eventDisable])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto disable = IntMethod(plugin.event);
			if (disable)
			{
				fakeMainThread.push([&disable](int) { ExceptionWrapper(disable)(); }).wait();
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupInviteReqEvent)
	{
		Unpack p;
		p.add(XQ_GroupInviteReqEvent);
		p.add(sourceId);
		p.add(activeQQ);
		p.add(rawMsg);
		const std::string data = base64_encode(p.getAll());
		for (const auto& plugin : plugins_events[CQ_eventRequest_AddGroup])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto invited = EvRequestAddGroup(plugin.event);
			if (invited)
			{
				if (invited(2, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), msg, data.c_str())) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupAddReqEvent)
	{
		Unpack p;
		p.add(XQ_GroupAddReqEvent);
		p.add(sourceId);
		p.add(activeQQ);
		p.add(rawMsg);
		const std::string data = base64_encode(p.getAll());
		for (const auto& plugin : plugins_events[CQ_eventRequest_AddGroup])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto addReq = EvRequestAddGroup(plugin.event);
			if (addReq)
			{
				if (addReq(1, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), msg, data.c_str())) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupInviteOtherReqEvent)
	{
		Unpack p;
		p.add(XQ_GroupInviteOtherReqEvent);
		p.add(sourceId);
		p.add(activeQQ);
		p.add(rawMsg);
		const std::string data = base64_encode(p.getAll());
		const string CQInviteMsg = "邀请人：[CQ:at,qq="s + activeQQ + "]" + ((strcmp(msg, "") != 0) ? (" 附言："s + msg) : "");
		for (const auto& plugin : plugins_events[CQ_eventRequest_AddGroup])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto addReq = EvRequestAddGroup(plugin.event);
			if (addReq)
			{
				if (addReq(1, atoi(timeStamp), atoll(sourceId), atoll(passiveQQ), CQInviteMsg.c_str(), data.c_str())) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_FriendAddReqEvent)
	{
		for (const auto& plugin : plugins_events[CQ_eventRequest_AddFriend])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto addReq = EvRequestAddFriend(plugin.event);
			if (addReq)
			{
				if (addReq(1, atoi(timeStamp), atoll(activeQQ), msg, activeQQ)) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupBanEvent)
	{
		int banTime = 0;
		std::string banTimeStr = msg;
		regex banTimeRegex("([0-9]+)天([0-9]+)时([0-9]+)分([0-9]+)秒", regex::ECMAScript);
		smatch m;
		if (regex_search(banTimeStr, m, banTimeRegex))
		{
			banTime = std::stoi(m[1]) * 86400 + std::stoi(m[2]) * 3600 + std::stoi(m[3]) * 60 + std::stoi(m[4]);
		}
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupBan])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto ban = EvGroupBan(plugin.event);
			if (ban)
			{
				if (ban(2, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), atoll(passiveQQ), banTime)) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupUnbanEvent)
	{
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupBan])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto ban = EvGroupBan(plugin.event);
			if (ban)
			{
				if (ban(1, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), atoll(passiveQQ), 0)) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupWholeBanEvent)
	{
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupBan])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto ban = EvGroupBan(plugin.event);
			if (ban)
			{
				if (ban(2, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), 0, 0)) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupWholeUnbanEvent)
	{
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupBan])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto ban = EvGroupBan(plugin.event);
			if (ban)
			{
				if (ban(1, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), 0, 0)) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupMemberIncreaseByApply)
	{
		GroupMemberCache.erase(atoll(sourceId));
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupMemberIncrease])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto MbrInc = EvGroupMember(plugin.event);
			if (MbrInc)
			{
				if (MbrInc(1, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), atoll(passiveQQ))) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupMemberIncreaseByInvite)
	{
		GroupMemberCache.erase(atoll(sourceId));
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupMemberIncrease])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto MbrInc = EvGroupMember(plugin.event);
			if (MbrInc)
			{
				if (MbrInc(2, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), atoll(passiveQQ))) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupMemberDecreaseByExit)
	{
		GroupMemberCache.erase(atoll(sourceId));
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupMemberDecrease])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto event = EvGroupMember(plugin.event);
			if (event)
			{
				if (event(1, atoi(timeStamp), atoll(sourceId), 0, atoll(passiveQQ)))  break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupMemberDecreaseByKick)
	{
		GroupMemberCache.erase(atoll(sourceId));
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupMemberDecrease])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto event = EvGroupMember(plugin.event);
			if (event)
			{
				if (robotQQ == atoll(passiveQQ))
				{
					if (event(3, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), atoll(passiveQQ))) break;
				}
				else
				{
					if (event(2, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), atoll(passiveQQ))) break;
				}
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupAdminSet)
	{
		GroupMemberCache.erase(atoll(sourceId));
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupAdmin])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto event = EvGroupAdmin(plugin.event);
			if (event)
			{
				if (event(2, atoi(timeStamp), atoll(sourceId), atoll(passiveQQ))) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupAdminUnset)
	{
		GroupMemberCache.erase(atoll(sourceId));
		for (const auto& plugin : plugins_events[CQ_eventSystem_GroupAdmin])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto event = EvGroupAdmin(plugin.event);
			if (event)
			{
				if (event(1, atoi(timeStamp), atoll(sourceId), atoll(passiveQQ))) break;
			}
		}
		return 0;
	}
	// 根据酷Q逻辑，只有不经过酷Q处理的好友添加事件（即比如用户设置了同意一切好友请求）才会调用好友已添加事件
	if (msgType == XQ_FriendAddedEvent)
	{
		for (const auto& plugin : plugins_events[CQ_eventFriend_Add])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto event = EvFriendAdd(plugin.event);
			if (event)
			{
				if (event(1, atoi(timeStamp), atoll(activeQQ))) break;
			}
		}
		return 0;
	}

	if (msgType == XQ_GroupFileUploadEvent)
	{
		std::string msgStr = msg;
		std::string sections = msgStr.substr(1, msgStr.size() - 2);

		// 转换为ID
		std::string id = retrieveSectionData(sections, "File");
		id = "/" + id.substr(1, id.size() - 2);

		// 文件名称
		std::string rawName = retrieveSectionData(sections, "name");
		std::string name;
		for (size_t i = 0; i < rawName.length(); i += 2)
		{
			string byte = rawName.substr(i, 2);
			name.push_back(static_cast<char> (strtol(byte.c_str(), nullptr, 16)));
		}
		name = UTF8toGB18030(name);

		// 文件大小
		long long size = 0;
		std::string rawMsgStr = rawMsg;
		int ByteLoc = rawMsgStr.find("42 79 74 65");
		if (ByteLoc != string::npos)
		{
			long long place = 1;
			while (ByteLoc >= 0)
			{
				ByteLoc -= 3;
				char c = static_cast<char>(strtol(rawMsgStr.substr(ByteLoc, 2).c_str(), nullptr, 16));
				if (isdigit(static_cast<unsigned char>(c)))
				{
					size += place * (c - 48LL);
				}
				else
				{
					break;
				}
				place *= 10;
			}
		}
		Unpack p;
		p.add(id);
		p.add(name);
		p.add(size);
		p.add(0LL);

		const std::string file = base64_encode(p.getAll());
		for (const auto& plugin : plugins_events[CQ_eventGroupUpload])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto event = EvGroupUpload(plugin.event);
			if (event)
			{
				if (event(1, atoi(timeStamp), atoll(sourceId), atoll(activeQQ), file.c_str())) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_groupCardChange)
	{
		GroupMemberCache.erase(atoll(sourceId));
	}

	// 如果不接收来自自己的事件, 直接退出函数
	if (activeQQ && !RecvSelfEvent && robotQQ == atoll(activeQQ))
	{
		return 0;
	}

	if (msgType == XQ_FriendMsgEvent || msgType == XQ_ShakeEvent)
	{
		size_t id = newMsgId({ 1, -1, atoll(activeQQ), atoll(msgNum), atoll(msgId), atoll(timeStamp) });
		for (const auto& plugin : plugins_events[CQ_eventPrivateMsg])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto privMsg = EvPriMsg(plugin.event);
			if (privMsg)
			{
				if (privMsg(11, id, atoll(activeQQ), (msgType == XQ_FriendMsgEvent) ? parseToCQCode(msg).c_str() : "[CQ:shake]", 0)) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_GroupTmpMsgEvent)
	{
		if (activeQQ && sourceId) UserGroupCache[atoll(activeQQ)] = atoll(sourceId);
		size_t id = newMsgId({ 4, atoll(sourceId), atoll(activeQQ), atoll(msgNum), atoll(msgId), atoll(timeStamp) });
		for (const auto& plugin : plugins_events[CQ_eventPrivateMsg])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto privMsg = EvPriMsg(plugin.event);
			if (privMsg)
			{
				if (privMsg(2, id, atoll(activeQQ), parseToCQCode(msg).c_str(), 0)) break;
			}
		}
	}
	if (msgType == XQ_GroupMsgEvent || (RecvSelfEvent && msgType == XQ_GroupSelfMsgEvent))
	{
		if (activeQQ && sourceId) UserGroupCache[atoll(activeQQ)] = atoll(sourceId);
		size_t id = newMsgId({ 2, atoll(sourceId), -1, atoll(msgNum), atoll(msgId), -1 });
		for (const auto& plugin : plugins_events[CQ_eventGroupMsg])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto groupMsg = EvGroupMsg(plugin.event);
			if (groupMsg)
			{
				if (groupMsg(1, id, atoll(sourceId), atoll(activeQQ), "", parseToCQCode(msg).c_str(), 0)) break;
			}
		}
		return 0;
	}
	if (msgType == XQ_DiscussTmpMsgEvent)
	{
		if (activeQQ && sourceId) UserDiscussCache[atoll(activeQQ)] = atoll(sourceId);
		size_t id = newMsgId({ 5, atoll(sourceId), atoll(activeQQ), atoll(msgNum), atoll(msgId), atoll(timeStamp) });
		for (const auto& plugin : plugins_events[CQ_eventPrivateMsg])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto privMsg = EvPriMsg(plugin.event);
			if (privMsg)
			{
				if (privMsg(3, id, atoll(activeQQ), parseToCQCode(msg).c_str(), 0)) break;
			}
		}
	}
	if (msgType == XQ_DiscussMsgEvent)
	{
		if (activeQQ && sourceId) UserDiscussCache[atoll(activeQQ)] = atoll(sourceId);
		size_t id = newMsgId({ 3, atoll(sourceId), -1, atoll(msgNum), atoll(msgId), -1 });
		for (const auto& plugin : plugins_events[CQ_eventDiscussMsg])
		{
			if (!plugins[plugin.plugin_id].enabled) continue;
			const auto event = EvDiscussMsg(plugin.event);
			if (event)
			{
				if (event(1, id, atoll(sourceId), atoll(activeQQ), parseToCQCode(msg).c_str(), 0)) break;
			}
		}
		return 0;
	}
	return 0;
}

#ifdef XQ
CQAPI(int32_t, XQ_Event, 48)(const char* botQQ, int32_t msgType, int32_t subType, const char* sourceId, const char* activeQQ, const char* passiveQQ, const char* msg, const char* msgNum, const char* msgId, const char* rawMsg, const char* timeStamp, char* retText)
#else
CQAPI(int32_t, OQ_Event, 48)(const char* botQQ, int32_t msgType, int32_t subType, const char* sourceId, const char* activeQQ, const char* passiveQQ, const char* msg, const char* msgNum, const char* msgId, const char* rawMsg, const char* timeStamp, char* retText)
#endif
{
	return ExceptionWrapper(CQXQ_process)(botQQ, msgType, subType, sourceId, activeQQ, passiveQQ, msg, msgNum, msgId, rawMsg, timeStamp, retText);
}


CQAPI(int32_t, CQ_canSendImage, 4)(int32_t)
{
	return 1;
}

CQAPI(int32_t, CQ_canSendRecord, 4)(int32_t)
{
	return 1;
}


CQAPI(int32_t, CQ_sendPrivateMsg, 16)(int32_t plugin_id, int64_t account, const char* msg)
{
	if (robotQQ == 0) return -1;
	if (!msg) return -1;
	std::string accStr = std::to_string(account);

	std::string ret;
	int type = 0;
	long long sourceId = 0;

	if (XQAPI::IfFriend(to_string(robotQQ).c_str(), accStr.c_str()))
	{
		type = 1;
		sourceId = -1;
	}
	else if (UserGroupCache.count(account))
	{
		type = 4;
		sourceId = UserGroupCache[account];
	}
	else if (UserDiscussCache.count(account))
	{
		type = 5;
		sourceId = UserDiscussCache[account];
	}
	else
	{
		XQAPI::OutPutLog(("无法发送消息给QQ" + accStr + ": 找不到可用的发送路径").c_str());
		return -1;
	}
	ret = parseCQCodeAndSend(type, std::to_string(sourceId).c_str(), accStr.c_str(), msg, 0, FALSE, FALSE, "");
	// 无法获取消息ID的强制成功，返回10e9
	if (ret == "FORCESUC")
	{
		return 1000000000;
	}
	try
	{
		nlohmann::json j = nlohmann::json::parse(ret);
		if (!j["sendok"].get<bool>())
		{
			return -1;
		}
		size_t msgId = newMsgId({ type, sourceId, account, j["msgno"].get<long long>(), j["msgid"].get<long long>(), j["msgtime"].get<long long>() });
		return msgId;
	}
	catch (std::exception&)
	{
		return -1;
	}
}

CQAPI(int32_t, CQ_sendGroupMsg, 16)(int32_t plugin_id, int64_t group, const char* msg)
{
	if (robotQQ == 0) return -1;
	if (!msg) return -1;
	if (XQAPI::IsShutUp(std::to_string(robotQQ).c_str(), std::to_string(group).c_str(), std::to_string(robotQQ).c_str()) ||
		XQAPI::IsShutUp(std::to_string(robotQQ).c_str(), std::to_string(group).c_str(), ""))
	{
		return -1;
	}

	// 匿名判断
	BOOL isAnon = FALSE;
	BOOL AnonIgnore = FALSE;
	std::string msgStr = msg;
	if (msgStr.substr(0, 13) == "[CQ:anonymous")
	{
		size_t r = msgStr.find(']');
		if (r != string::npos)
		{
			isAnon = TRUE;
			std::string anonOptions = msgStr.substr(13, r - 13);
			std::string ignore = retrieveSectionData(anonOptions, "ignore");
			if (ignore == "true")
			{
				AnonIgnore = TRUE;
			}
			msgStr = msgStr.substr(r + 1);
		}
	}
	std::string grpStr = std::to_string(group);
	std::string ret = parseCQCodeAndSend(2, grpStr.c_str(), to_string(robotQQ).c_str(), msgStr.c_str(), 0, isAnon, AnonIgnore, "");
	// 无法获取消息ID的强制成功，返回10e9
	if (ret == "FORCESUC")
	{
		return 1000000000;
	}
	try
	{
		nlohmann::json j = nlohmann::json::parse(ret);
		if (!j["sendok"].get<bool>())
		{
			return -1;
		}
		size_t msgId = newMsgId({ 2, group, -1, j["msgno"].get<long long>(), j["msgid"].get<long long>(), -1 });
		return msgId;
	}
	catch (std::exception&)
	{
		return -1;
	}
}

CQAPI(int32_t, CQ_setFatal, 8)(int32_t plugin_id, const char* info)
{
	XQAPI::OutPutLog((plugins[plugin_id].file + ": [FATAL] " + info).c_str());
	return 0;
}

CQAPI(const char*, CQ_getAppDirectory, 4)(int32_t plugin_id)
{
	std::string ppath;
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string pathStr(path);
	ppath = pathStr.substr(0, pathStr.rfind('\\')) + "\\CQPlugins\\config\\" + plugins[plugin_id].file + "\\";
	std::filesystem::create_directories(ppath);
	return delayMemFreeCStr(ppath.c_str());
}

CQAPI(int64_t, CQ_getLoginQQ, 4)(int32_t plugin_id)
{
	return robotQQ;
}

CQAPI(const char*, CQ_getLoginNick, 4)(int32_t plugin_id)
{
	const char* nick = XQAPI::GetNick(to_string(robotQQ).c_str(), to_string(robotQQ).c_str());
	return nick ? delayMemFreeCStr(nickToCQCode(nick)) : "";
}

CQAPI(int32_t, CQ_setGroupAnonymous, 16)(int32_t plugin_id, int64_t group, BOOL enable)
{
	XQAPI::SetAnon(to_string(robotQQ).c_str(), std::to_string(group).c_str(), enable);
	return 0;
}

CQAPI(int32_t, CQ_setGroupBan, 28)(int32_t plugin_id, int64_t group, int64_t member, int64_t duration)
{
	XQAPI::ShutUP(to_string(robotQQ).c_str(), std::to_string(group).c_str(), std::to_string(member).c_str(), static_cast<int32_t>(duration));
	return 0;
}

CQAPI(int32_t, CQ_setGroupCard, 24)(int32_t plugin_id, int64_t group, int64_t member, const char* card)
{
	XQAPI::SetGroupCard(to_string(robotQQ).c_str(), std::to_string(group).c_str(), std::to_string(member).c_str(), card);
	return 0;
}

CQAPI(int32_t, CQ_setGroupKick, 24)(int32_t plugin_id, int64_t group, int64_t member, BOOL reject)
{
	XQAPI::KickGroupMBR(to_string(robotQQ).c_str(), std::to_string(group).c_str(), std::to_string(member).c_str(), reject);
	return 0;
}

CQAPI(int32_t, CQ_setGroupLeave, 16)(int32_t plugin_id, int64_t group, BOOL dismiss)
{
	XQAPI::QuitGroup(to_string(robotQQ).c_str(), std::to_string(group).c_str());
	return 0;
}

CQAPI(int32_t, CQ_setGroupSpecialTitle, 32)(int32_t plugin_id, int64_t group, int64_t member,
                                            const char* title, int64_t duration)
{
	XQAPI::OutPutLog((plugins[plugin_id].file + "调用了不支持的API CQ_setGroupSpecialTitle").c_str());
	return 0;
}

CQAPI(int32_t, CQ_setGroupWholeBan, 16)(int32_t plugin_id, int64_t group, BOOL enable)
{
	XQAPI::ShutUP(to_string(robotQQ).c_str(), std::to_string(group).c_str(), "", enable);
	return 0;
}

CQAPI(int32_t, CQ_deleteMsg, 12)(int32_t plugin_id, int64_t msg_id)
{
	size_t id = static_cast<size_t>(msg_id);
	FakeMsgId msgId;
	{
		std::unique_lock lock(memFreeMsgIdMutex);
		if (msgIdMap.count(id))
		{
			msgId = msgIdMap[id];
			msgIdMap.erase(id);
		}
		else
		{
			return -1;
		}
	}
	XQAPI::WithdrawMsgEX(to_string(robotQQ).c_str(),
		msgId.type,
		msgId.sourceId == -1 ? "" : std::to_string(msgId.sourceId).c_str(),
		msgId.QQ == -1 ? "" : std::to_string(msgId.QQ).c_str(),
		std::to_string(msgId.msgNum).c_str(),
		std::to_string(msgId.msgId).c_str(),
		msgId.msgTime == -1 ? "" : std::to_string(msgId.msgTime).c_str()
	);
	return 0;
}

CQAPI(const char*, CQ_getFriendList, 8)(int32_t plugin_id, BOOL reserved)
{
	std::string ret;
	const char* frdLst = XQAPI::GetFriendList(to_string(robotQQ).c_str());
	if (!frdLst)return "";
	std::string friendList = frdLst;
	Unpack p;
	std::vector<Unpack> Friends;
	int count = 0;
	try
	{
		nlohmann::json j = nlohmann::json::parse(friendList);
		for (const auto& item : j["result"])
		{
			for (const auto& member : item["mems"])
			{
				Unpack t;
				t.add(member["uin"].get<long long>());
				t.add(UTF8toGB18030(member["name"].get<std::string>()));
				t.add("");
				Friends.push_back(t);
				count++;
			}
		}
		p.add(count);
		for (auto& g : Friends)
		{
			p.add(g);
		}
		ret = base64_encode(p.getAll());
		return delayMemFreeCStr(ret.c_str());
	}
	catch(std::exception&)
	{
		return "";
	}
	return "";
	

	/*
	std::string ret;
	const char* frdLst = XQAPI::GetFriendList_B(to_string(robotQQ).c_str());
	if (!frdLst) return "";
	std::string friendList = frdLst;
	Unpack p;
	std::vector<Unpack> Friends;
	int count = 0;
	while (!friendList.empty())
	{
		size_t endline = friendList.find('\n');
		std::string item = friendList.substr(0, endline);
		while (!item.empty() && (item[item.length() - 1] == '\r' || item[item.length() - 1] == '\n')) item.erase(item.end() - 1);
		if(!item.empty())
		{
			Unpack tmp;
			tmp.add(atoll(item.c_str()));
			const char* nick = XQAPI::GetNick(to_string(robotQQ).c_str(), item.c_str());
			tmp.add(nick ? nick : "");
			const char* remarks = XQAPI::GetFriendsRemark(to_string(robotQQ).c_str(), item.c_str());
			tmp.add(remarks ? remarks : "");
			Friends.push_back(tmp);
			count++;
		}
		if (endline == string::npos) friendList = "";
		else friendList = friendList.substr(endline + 1);
	}
	p.add(count);
	for (auto& g : Friends)
	{
		p.add(g);
	}
	ret = base64_encode(p.getAll());
	return delayMemFreeCStr(ret.c_str());
	*/
}

CQAPI(const char*, CQ_getGroupInfo, 16)(int32_t plugin_id, int64_t group, BOOL disableCache)
{
	std::string ret;

	// 判断是否是新获取的列表
	bool newRetrieved = false;
	std::string memberListStr;

	// 判断是否要使用缓存
	if (disableCache || !GroupMemberCache.count(group) || (time(nullptr) - GroupMemberCache[group].second) > 3600)
	{
		newRetrieved = true;
		const char* memberList = XQAPI::GetGroupMemberList_B(to_string(robotQQ).c_str(), std::to_string(group).c_str());
		memberListStr = memberList ? memberList : "";
	}
	else
	{
		memberListStr = GroupMemberCache[group].first;
	}

	try
	{
		if (memberListStr.empty())
		{
			throw std::runtime_error("GetGroupMemberList Failed");
		}
		nlohmann::json j = nlohmann::json::parse(memberListStr);
		std::string groupStr = std::to_string(group);
		const char* groupName = XQAPI::GetGroupName(to_string(robotQQ).c_str(), groupStr.c_str());
		std::string groupNameStr = groupName ? nickToCQCode(groupName) : "";
		int currentNum = j["mem_num"].get<int>();
		int maxNum = j["max_num"].get<int>();
		int friendNum = 0;
		for (const auto& member : j["members"].items())
		{
			if (member.value().count("fr") && member.value()["fr"].get<int>() == 1)
			{
				friendNum += 1;
			}
		}
		Unpack p;
		p.add(group);
		p.add(groupNameStr);
		p.add(currentNum);
		p.add(maxNum);
		p.add(friendNum);
		ret = base64_encode(p.getAll());
		if (newRetrieved) GroupMemberCache[group] = { memberListStr, time(nullptr) };
		return delayMemFreeCStr(ret.c_str());
	}
	catch (std::exception&)
	{
		XQAPI::OutPutLog(("警告, 获取群信息失败, 正在使用更慢的另一种方法尝试: "s + memberListStr).c_str());
		std::string groupStr = std::to_string(group);
		const char* groupName = XQAPI::GetGroupName(to_string(robotQQ).c_str(), groupStr.c_str());
		std::string groupNameStr = groupName ? nickToCQCode(groupName) : "";
		const char* groupNum = XQAPI::GetGroupMemberNum(to_string(robotQQ).c_str(), groupStr.c_str());
		int currentNum = 0, maxNum = 0;
		if (groupNum)
		{
			std::string groupNumStr = groupNum;
			size_t newline = groupNumStr.find('\n');
			if (newline != string::npos)
			{
				currentNum = atoi(groupNumStr.substr(0, newline).c_str());
				maxNum = atoi(groupNumStr.substr(newline + 1).c_str());
			}
		}
		Unpack p;
		p.add(group);
		p.add(groupNameStr);
		p.add(currentNum);
		p.add(maxNum);
		p.add(0); // 这种方式暂不支持好友人数
		ret = base64_encode(p.getAll());
		return delayMemFreeCStr(ret.c_str());
	}
}

CQAPI(const char*, CQ_getGroupList, 4)(int32_t plugin_id)
{
	std::string ret;

	const char* groupList = XQAPI::GetGroupList(to_string(robotQQ).c_str());
	std::string groupListStr = groupList ? groupList : "";
	if (groupListStr.empty()) return "";
	try
	{
		Unpack p;
		std::vector<Unpack> Groups;
		nlohmann::json j = nlohmann::json::parse(groupListStr);
		for (const auto& group : j["create"])
		{
			Unpack t;
			t.add(group["gc"].get<long long>());
			t.add(UTF8toGB18030(group["gn"].get<std::string>()));
			Groups.push_back(t);
		}
		for (const auto& group : j["join"])
		{
			Unpack t;
			t.add(group["gc"].get<long long>());
			t.add(UTF8toGB18030(group["gn"].get<std::string>()));
			Groups.push_back(t);
		}
		for (const auto& group : j["manage"])
		{
			Unpack t;
			t.add(group["gc"].get<long long>());
			t.add(UTF8toGB18030(group["gn"].get<std::string>()));
			Groups.push_back(t);
		}
		p.add(static_cast<int>(Groups.size()));
		for (auto& group : Groups)
		{
			p.add(group);
		}
		ret = base64_encode(p.getAll());
		return delayMemFreeCStr(ret.c_str());
	}
	catch (std::exception&)
	{
		XQAPI::OutPutLog(("警告, 获取群列表失败: "s + groupListStr).c_str());
		return "";
	}
}

CQAPI(const char*, CQ_getGroupMemberInfoV2, 24)(int32_t plugin_id, int64_t group, int64_t account, BOOL disableCache)
{
	std::string ret;
	// 判断是否是新获取的列表
	bool newRetrieved = false;
	std::string memberListStr;

	// 判断是否要使用缓存
	if (disableCache || !GroupMemberCache.count(group) || (time(nullptr) - GroupMemberCache[group].second) > 3600)
	{
		newRetrieved = true;
		const char* memberList = XQAPI::GetGroupMemberList_B(to_string(robotQQ).c_str(), std::to_string(group).c_str());
		memberListStr = memberList ? memberList : "";
	}
	else
	{
		memberListStr = GroupMemberCache[group].first;
	}

	try
	{
		std::string accStr = std::to_string(account);
		if (memberListStr.empty())
		{
			throw std::runtime_error("GetGroupMemberList Failed");
		}
		nlohmann::json j = nlohmann::json::parse(memberListStr);
		long long owner = j["owner"].get<long long>();
		std::set<long long> admin;
		if (j.count("adm")) j["adm"].get_to(admin);
		std::map<std::string, std::string> lvlName = j["levelname"].get<std::map<std::string, std::string>>();
		for (auto& item : lvlName)
		{
			lvlName[item.first] = UTF8toGB18030(item.second);
		}
		if (!j["members"].count(accStr)) return "";
		Unpack t;
		t.add(group);
		t.add(account);
		t.add(j["members"][accStr].count("nk") ? UTF8toGB18030(j["members"][accStr]["nk"].get<std::string>()) : "");
		t.add(j["members"][accStr].count("cd") ? UTF8toGB18030(j["members"][accStr]["cd"].get<std::string>()) : "");
		t.add(255);
		t.add(-1);
		/*
		int gender = XQAPI::GetGender(to_string(robotQQ).c_str(), accStr.c_str());
		t.add(gender == -1 ? 255 : -1);
		t.add(XQAPI::GetAge(to_string(robotQQ).c_str(), accStr.c_str()));
		*/
		t.add("");
		t.add(j["members"][accStr].count("jt") ? j["members"][accStr]["jt"].get<int>() : 0);
		t.add(j["members"][accStr].count("lst") ? j["members"][accStr]["lst"].get<int>() : 0);
		t.add(j["members"][accStr].count("ll") ? (lvlName.count("lvln" + std::to_string(j["members"][accStr]["ll"].get<int>())) ? lvlName["lvln" + std::to_string(j["members"][accStr]["ll"].get<int>())] : "") : "");
		t.add(account == owner ? 3 : (admin.count(account) ? 2 : 1));
		t.add(FALSE);
		t.add("");
		t.add(-1);
		t.add(TRUE);
		ret = base64_encode(t.getAll());
		if (newRetrieved) GroupMemberCache[group] = { memberListStr, time(nullptr) };
		return delayMemFreeCStr(ret.c_str());
	}
	catch (std::exception&)
	{
		XQAPI::OutPutLog(("警告, 获取群成员信息失败, 正在使用更慢的另一种方法尝试: "s + memberListStr).c_str());
		std::string grpStr = std::to_string(group);
		std::string accStr = std::to_string(account);
		Unpack p;
		p.add(group);
		p.add(account);
		const char* nick = XQAPI::GetNick(to_string(robotQQ).c_str(), accStr.c_str());
		p.add(nick ? nickToCQCode(nick) : "");
		const char* groupCard = XQAPI::GetGroupCard(to_string(robotQQ).c_str(), grpStr.c_str(), accStr.c_str());
		p.add(groupCard ? nickToCQCode(groupCard) : "");
		p.add(255);
		p.add(-1);
		/*
		int gender = XQAPI::GetGender(to_string(robotQQ).c_str(), accStr.c_str());
		p.add(gender == -1 ? 255 : -1);
		p.add(XQAPI::GetAge(to_string(robotQQ).c_str(), accStr.c_str()));
		*/
		p.add("");
		p.add(0);
		p.add(0);
		p.add("");
		const char* admin = XQAPI::GetGroupAdmin(to_string(robotQQ).c_str(), std::to_string(group).c_str());
		std::string adminList = admin ? admin : "";
		int count = 0;
		int permissions = 1;
		while (!adminList.empty())
		{
			size_t endline = adminList.find('\n');
			std::string item = adminList.substr(0, endline);
			while (!item.empty() && (item[item.length() - 1] == '\r' || item[item.length() - 1] == '\n')) item.erase(item.end() - 1);
			if (item == accStr)
			{
				if (count == 0)permissions = 3;
				else permissions = 2;
				break;
			}
			if (endline == string::npos) adminList = "";
			else adminList = adminList.substr(endline + 1);
			count++;
		}
		p.add(permissions);
		p.add(FALSE);
		p.add("");
		p.add(-1);
		p.add(TRUE);
		ret = base64_encode(p.getAll());
		return delayMemFreeCStr(ret.c_str());
	}
}

CQAPI(const char*, CQ_getGroupMemberList, 12)(int32_t plugin_id, int64_t group)
{
	std::string ret;
	const char* memberList = XQAPI::GetGroupMemberList_B(to_string(robotQQ).c_str(), std::to_string(group).c_str());
	std::string memberListStr = memberList ? memberList : "";
	try
	{
		if (memberListStr.empty())
		{
			throw std::runtime_error("GetGroupMemberList Failed");
		}
		Unpack p;
		nlohmann::json j = nlohmann::json::parse(memberListStr);
		long long owner = j["owner"].get<long long>();
		std::set<long long> admin;
		if (j.count("adm")) j["adm"].get_to(admin);
		int mem_num = j["mem_num"].get<int>();
		std::map<std::string, std::string> lvlName = j["levelname"].get<std::map<std::string, std::string>>();
		for (auto& item : lvlName)
		{
			lvlName[item.first] = UTF8toGB18030(item.second);
		}
		p.add(mem_num);
		for (const auto& member : j["members"].items())
		{
			long long qq = std::stoll(member.key());
			Unpack t;
			t.add(group);
			t.add(qq);
			t.add(member.value().count("nk") ? UTF8toGB18030(member.value()["nk"].get<std::string>()) : "");
			t.add(member.value().count("cd") ? UTF8toGB18030(member.value()["cd"].get<std::string>()) : "");
			t.add(255);
			t.add(-1);
			/*
			int gender = XQAPI::GetGender(to_string(robotQQ).c_str(), member.key().c_str());
			t.add(gender == -1 ? 255 : -1);
			t.add(XQAPI::GetAge(to_string(robotQQ).c_str(), member.key().c_str()));
			*/
			t.add("");
			t.add(member.value().count("jt") ? member.value()["jt"].get<int>() : 0);
			t.add(member.value().count("lst") ? member.value()["lst"].get<int>() : 0);
			t.add(member.value().count("ll") ? (lvlName.count("lvln" + std::to_string(member.value()["ll"].get<int>())) ? lvlName["lvln" + std::to_string(member.value()["ll"].get<int>())]: "") : "");
			t.add(qq == owner ? 3 : (admin.count(qq) ? 2 : 1));
			t.add(FALSE);
			t.add("");
			t.add(-1);
			t.add(TRUE);
			p.add(t);
		}
		GroupMemberCache[group] = { memberListStr, time(nullptr) };
		ret = base64_encode(p.getAll());
		return delayMemFreeCStr(ret.c_str());
	}
	catch (std::exception&)
	{
		XQAPI::OutPutLog(("警告, 获取群成员列表失败: "s + memberListStr).c_str());
		return "";
	}
	return "";
}

CQAPI(const char*, CQ_getCookiesV2, 8)(int32_t plugin_id, const char* domain)
{
	return XQAPI::GetCookies(to_string(robotQQ).c_str());
}

CQAPI(const char*, CQ_getCsrfToken, 4)(int32_t plugin_id)
{
	return XQAPI::GetBkn(to_string(robotQQ).c_str());
}

CQAPI(const char*, CQ_getImage, 8)(int32_t plugin_id, const char* file)
{
	if (!file) return "";
	std::string fileStr(file);
	if (fileStr.empty()) return "";
	if (fileStr.substr(0, 10) == "[CQ:image," && fileStr[fileStr.length() - 1] == ']')
	{
		fileStr = fileStr.substr(10, fileStr.length() - 10 - 1);
		// 现在的状态是file=xxx.jpg/png/gif/...(,cache=xxx)
		size_t file_loc = fileStr.find("file=");
		if (file_loc != string::npos)
		{
			fileStr = fileStr.substr(file_loc + 5, fileStr.find(',', file_loc) - file_loc - 5);
		}
	}
	else if (fileStr.substr(0, 5) == "[pic=" && fileStr[fileStr.length() - 1] == ']')
	{
		fileStr = fileStr.substr(5, fileStr.length() - 5 - 1);
	}

	const char* picLink;
	std::string picFileName;
	// 现在是图片名本身，判断是否符合格式, 并判断是好友图片还是群聊图片
	regex groupPic("\\{([0-9A-Fa-f]{8})[-]([0-9A-Fa-f]{4})[-]([0-9A-Fa-f]{4})[-]([0-9A-Fa-f]{4})[-]([0-9A-Fa-f]{12})\\}\\.(jpg|png|gif|bmp|jpeg).*", regex::ECMAScript | regex::icase);
	regex privatePic("\\{[0-9]{5,15}[-][0-9]{5,15}[-]([0-9A-Fa-f]{32})\\}\\.(jpg|png|gif|bmp|jpeg).*", regex::ECMAScript | regex::icase);
	smatch m;
	if (regex_match(fileStr, m, groupPic))
	{
		fileStr = "[pic=" + fileStr + "]";
		picFileName = m[1].str() + m[2].str() + m[3].str() + m[4].str() + m[5].str() + "." + m[6].str();
		// 群号其实并没有用，随便写一个
		picLink = XQAPI::GetPicLink(to_string(robotQQ).c_str(), 2, "173528463", fileStr.c_str());
	}
	else if (regex_match(fileStr, m, privatePic))
	{
		fileStr = "[pic=" + fileStr + "]";
		picFileName = m[1].str() + "." + m[2].str();
		picLink = XQAPI::GetPicLink(to_string(robotQQ).c_str(), 1, "", fileStr.c_str());
	}
	else
	{
		return "";
	}

	if (!picLink || strcmp(picLink, "") == 0)
	{
		return "";
	}
	std::string path = rootPath + "\\data\\image\\" + picFileName;
	Cominit init;
	if (filesystem::exists(path) || URLDownloadToFileA(nullptr, picLink, (path).c_str(), 0, nullptr) == S_OK)
	{
		return delayMemFreeCStr(path);
	}
	return "";
}

CQAPI(const char*, CQ_getRecordV2, 12)(int32_t plugin_id, const char* file, const char* format)
{
	if (!file) return "";
	std::string fileStr(file);
	std::string recordName;
	if (fileStr.empty()) return "";
	if (fileStr.substr(0, 16) == "[CQ:record,file=")
	{
		fileStr = "[Voi=" + fileStr.substr(16, fileStr.length() - 1 - 16) + "]";
		recordName = fileStr.substr(16, fileStr.length() - 1 - 16);
	}
	else if (fileStr.substr(0, 5) == "[Voi=")
	{
		recordName = fileStr.substr(5, fileStr.length() - 1 - 5);
	}
	else
	{
		return "";
	}

	const char* recordLink = XQAPI::GetVoiLink(to_string(robotQQ).c_str(), fileStr.c_str());
	if (!recordLink || strcmp(recordLink, "") == 0)
	{
		return "";
	}
	std::string path = rootPath + "\\data\\record\\" + recordName;
	Cominit init;
	if (filesystem::exists(path) || URLDownloadToFileA(nullptr, recordLink, path.c_str(), 0, nullptr) == S_OK)
	{
		return delayMemFreeCStr(path);
	}
	
	return "";
}

CQAPI(const char*, CQ_getStrangerInfo, 16)(int32_t plugin_id, int64_t account, BOOL disableCache)
{
	std::string ret;
	std::string accStr = std::to_string(account);
	Unpack p;
	p.add(account);
	const char* nick = XQAPI::GetNick(to_string(robotQQ).c_str(), accStr.c_str());
	p.add(nick ? nickToCQCode(nick) : "");
	p.add(255);
	p.add(-1);
	/*
	int gender = XQAPI::GetGender(to_string(robotQQ).c_str(), accStr.c_str());
	p.add(gender == -1 ? 255 : gender);
	p.add(XQAPI::GetAge(to_string(robotQQ).c_str(), accStr.c_str()));
	*/
	ret = base64_encode(p.getAll());
	return delayMemFreeCStr(ret.c_str());
}

CQAPI(int32_t, CQ_sendDiscussMsg, 16)(int32_t plugin_id, int64_t discuss, const char* msg)
{
	if (robotQQ == 0) return -1;
	if (!msg) return -1;
	std::string discussStr = std::to_string(discuss);
	std::string ret = parseCQCodeAndSend(3, discussStr.c_str(), to_string(robotQQ).c_str(), msg, 0, FALSE, FALSE, "");
	// 无法获取消息ID的强制成功，返回10e9
	if (ret == "FORCESUC")
	{
		return 1000000000;
	}
	try
	{
		nlohmann::json j = nlohmann::json::parse(ret);
		if (!j["sendok"].get<bool>())
		{
			return -1;
		}
		size_t msgId = newMsgId({ 3, discuss, -1, j["msgno"].get<long long>(), j["msgid"].get<long long>(), -1 });
		return msgId;
	}
	catch (std::exception&)
	{
		return -1;
	}
}

CQAPI(int32_t, CQ_sendLikeV2, 16)(int32_t plugin_id, int64_t account, int32_t times)
{
	std::string accStr = std::to_string(account);
	for (int i=0;i!=times;i++)
	{
		XQAPI::UpVote(to_string(robotQQ).c_str(), accStr.c_str());
	}
	return 0;
}

CQAPI(int32_t, CQ_setDiscussLeave, 12)(int32_t plugin_id, int64_t discuss)
{
	XQAPI::OutPutLog((plugins[plugin_id].file + "调用了不支持的API CQ_setDiscussLeave").c_str());
	return 0;
}

CQAPI(int32_t, CQ_setFriendAddRequest, 16)(int32_t plugin_id, const char* id, int32_t type, const char* remark)
{
	XQAPI::HandleFriendEvent(to_string(robotQQ).c_str(), id, type == 1 ? 10 : 20, remark);
	return 0;
}

CQAPI(int32_t, CQ_setGroupAddRequestV2, 20)(int32_t plugin_id, const char* id, int32_t req_type, int32_t fb_type,
                                            const char* reason)
{
	Unpack p(base64_decode(id));
	int eventType = p.getInt();
	std::string group = p.getstring();
	std::string qq = p.getstring();
	std::string raw = p.getstring();
	XQAPI::HandleGroupEvent(to_string(robotQQ).c_str(), eventType, qq.c_str(), group.c_str(), raw.c_str(), fb_type == 1 ? 10 : 20, reason);
	return 0;
}

CQAPI(int32_t, CQ_setGroupAdmin, 24)(int32_t plugin_id, int64_t group, int64_t account, BOOL admin)
{
	// https://qinfo.clt.qq.com/cgi-bin/qun_info/set_group_admin
	XQAPI::OutPutLog((plugins[plugin_id].file + "调用了不支持的API CQ_setAdmin").c_str());
	// XQAPI::SetAdmin(to_string(robotQQ).c_str(), std::to_string(group).c_str(), std::to_string(account).c_str(), admin);
	return 0;
}

CQAPI(int32_t, CQ_setGroupAnonymousBan, 24)(int32_t plugin_id, int64_t group, const char* id, int64_t duration)
{
	XQAPI::OutPutLog((plugins[plugin_id].file + "调用了不支持的API CQ_setGroupAnonymousBan").c_str());
	return 0;
}

CQAPI(int32_t, CQ_addLog, 16)(int32_t plugin_id, int32_t priority, const char* type, const char* content)
{
	string level;
	switch(priority)
	{
	case 0:
		level = "DEBUG";
		break;
	case 10:
		level = "INFO";
		break;
	case 11:
		level = "INFOSUCCESS";
		break;
	case 12:
		level = "INFORECV";
		break;
	case 13:
		level = "INFOSEND";
		break;
	case 20:
		level = "WARNING";
		break;
	case 30:
		level = "ERROR";
		break;
	case 40:
		level = "FATAL";
		break;
	default:
		level = "UNKNOWN";
		break;
	}
	XQAPI::OutPutLog((plugins[plugin_id].file + ": [" + level + "] [" + type + "] " + content).c_str());
	return 0;
}

// Legacy

CQAPI(const char*, CQ_getCookies, 4)(int32_t plugin_id)
{
	return CQ_getCookiesV2(plugin_id, "");
}

CQAPI(int32_t, CQ_setGroupAddRequest, 16)(int32_t plugin_id, const char* id, int32_t req_type, int32_t fb_type)
{
	return CQ_setGroupAddRequestV2(plugin_id, id, req_type, fb_type, "");
}

CQAPI(int32_t, CQ_sendLike, 12)(int32_t plugin_id, int64_t account)
{
	return CQ_sendLikeV2(plugin_id, account, 1);
}

CQAPI(int32_t, CQ_reload, 4)(int32_t plugin_id)
{
	reloadOneCQPlugin(plugin_id);
	return 0;
}

