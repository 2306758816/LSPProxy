#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <ws2spi.h>  
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <errno.h>  
#include <fstream>
#pragma   comment(lib,"Ws2_32.lib")  

// !< ��LSPProxy�ж����GUIDһ��, �ڱ���LSPЭ�鴦����ʱ������.
GUID ProviderGuid = { 0xd3c21122, 0x85e1, 0x48f3,{ 0x9a,0xb6,0x23,0xd9,0x0c,0x73,0x77,0xef } };

LPWSAPROTOCOL_INFOW  ProtoInfo = NULL;
WSPPROC_TABLE        NextProcTable;
DWORD                ProtoInfoSize = 0;
int                  TotalProtos = 0;
// �������  
int PutDbgStr(LPCTSTR lpFmt, ...)
{
	TCHAR  Msg[1024];
	int  len = wvsprintf(Msg, lpFmt, va_list(1 + &lpFmt));
	OutputDebugString(Msg);
	return len;
}
// ��ȡ����ֵ  
BOOL GetLSP()
{
	int    errorcode;
	ProtoInfo = NULL;
	ProtoInfoSize = 0;
	TotalProtos = 0;
	if (WSCEnumProtocols(NULL, ProtoInfo, &ProtoInfoSize, &errorcode) == SOCKET_ERROR)
	{
		if (errorcode != WSAENOBUFS)
		{
			PutDbgStr(L"First WSCEnumProtocols Error!");
			return FALSE;
		}
	}
	if ((ProtoInfo = (LPWSAPROTOCOL_INFOW)GlobalAlloc(GPTR, ProtoInfoSize)) == NULL)
	{
		PutDbgStr(L"GlobalAlloc Error!");
		return FALSE;
	}
	if ((TotalProtos = WSCEnumProtocols(NULL, ProtoInfo, &ProtoInfoSize, &errorcode)) == SOCKET_ERROR)
	{
		PutDbgStr(L"Second WSCEnumProtocols Error!");
		return FALSE;
	}
	return TRUE;
}
// �ͷ��ڴ�  
void FreeLSP()
{
	GlobalFree(ProtoInfo);
}
/********************************* ��дWSP������ֻ��WSPConnect����д�ɵ���ProxyConnect������������ֱ�ӵ����²�WSP���� ****************************************/


// --- SOCKS5������ ---
// ����socks5����  
int ProxyConnect(SOCKET s, const struct sockaddr *name, int namelen)
{
	int rc = 0;
	// ����Ӧ���ȱ�����socket������/���������ͣ����������������ֵ������ԭ�����ǲ�֪��������ȡ������  
	// �޸�socketΪ��������  
	if (rc = WSAEventSelect(s, 0, NULL))//��һ�����Բ���ִ��  
	{
		PutDbgStr(L"Error %d : WSAEventSelect Failure!", WSAGetLastError());
	}
	else
	{
		PutDbgStr(L"Message : WSAEventSelect successfully!");
	}
	unsigned long nonBlock = 0;
	if (rc = ioctlsocket(s, FIONBIO, &nonBlock))// ��������޸�Ϊ��������  
	{
		PutDbgStr(L"Error %d : Set Blocking Failure!", WSAGetLastError());
	}
	else
	{
		PutDbgStr(L"Message : Set Blocking successfully!");
	}
	//���Ӵ��������  
	sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;

	// !< TODO:�������
	inet_pton(AF_INET, "127.0.0.1", (void*)&serveraddr.sin_addr);
	serveraddr.sin_port = htons(10801); // �˿ں�  

	WSABUF DataBuf;
	char buffer[4];
	memset(buffer, 0, sizeof(buffer));
	DataBuf.len = 4;
	DataBuf.buf = buffer;
	int err = 0;
	if ((rc = NextProcTable.lpWSPConnect(s, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr), &DataBuf, NULL, NULL, NULL, &err)) != 0)
	{
		PutDbgStr(L"Error %d : attempting to connect to SOCKS server!", err);
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : Connect to SOCKS server successfully!");
	}
	//����������Э�̰汾����֤����  
	//VER   NMETHODS    METHODS  
	//1     1           1 to 255  
	char verstring[257];
	verstring[0] = 0x05;    //VER (1 Byte)  
	verstring[1] = 0x01;    //NMETHODS (1 Byte)  
	verstring[2] = 0x00;    //METHODS (allow 1 - 255 bytes, current 1 byte)  
	if ((rc = send(s, verstring, 3, 0)) < 0)
	{
		PutDbgStr(L"Error %d : attempting to send SOCKS method negotiation!", WSAGetLastError());
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : send SOCKS method negotiation successfully!");
	}
	//���մ��������������Ϣ  
	//VER   METHOD  
	//1     1  
	/*��ǰ����ķ����У�
	�� X��00�� ����Ҫ��֤
	�� X��01�� GSSAPI
	�� X��02�� �û���/����
	�� X��03�� -- X��7F�� ��IANA����
	�� X��80�� -- X��FE�� Ϊ˽�˷�����������
	�� X��FF�� û�п��Խ��ܵķ���*/
	if ((rc = recv(s, verstring, 257, 0)) < 0)
	{
		PutDbgStr(L"Error %d : attempting to receive SOCKS method negotiation reply!", WSAGetLastError());
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : receive SOCKS method negotiation reply successfully!");
	}
	if (rc < 2)//����2�ֽ�  
	{
		PutDbgStr(L"Error : Short reply from SOCKS server!");
		rc = ECONNREFUSED;
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : reply from SOCKS server larger than 2");
	}
	// ���������ѡ�񷽷�  
	// �ж����ǵķ����Ƿ����  
	if (verstring[1] == 0xff)
	{
		PutDbgStr(L"Error : SOCKS server refused authentication methods!");
		rc = ECONNREFUSED;
		return rc;
	}
	else if (verstring[1] == 0x02)// ����2 �� �û���/����  
	{
		//���⴦��  
		PutDbgStr(L"Error : SOCKS server need username/password!");
	}
	else if (verstring[1] == 0x00)// ����0�� ����Ҫ��֤  
	{
		//����SOCKS����  
		//VER   CMD RSV     ATYP    DST.ADDR    DST.PROT  
		//1     1   X'00'   1       Variable    2  
		/* VER Э��汾: X��05��
		�� CMD
		�� CONNECT��X��01��
		�� BIND��X��02��
		�� UDP ASSOCIATE��X��03��
		�� RSV ����
		�� ATYP ����ĵ�ַ����
		�� IPV4��X��01��
		�� ������X��03��
		�� IPV6��X��04��'
		�� DST.ADDR Ŀ�ĵ�ַ
		�� DST.PORT �������ֽ�˳����ֵĶ˿ں�
		SOCKS�����������Դ��ַ��Ŀ�ĵ�ַ����������Ȼ������������ͷ���һ������Ӧ��*/
		struct sockaddr_in sin;
		sin = *(const struct sockaddr_in *)name;
		char buf[10];
		buf[0] = 0x05; // �汾 SOCKS5  
		buf[1] = 0x01; // ��������  
		buf[2] = 0x00; // �����ֶ�  
		buf[3] = 0x01; // IPV4  
		memcpy(&buf[4], &sin.sin_addr.S_un.S_addr, 4);
		memcpy(&buf[8], &sin.sin_port, 2);
		//����  
		if ((rc = send(s, buf, 10, 0)) < 0)
		{
			PutDbgStr(L"Error %d : attempting to send SOCKS connect command!", WSAGetLastError());
			return rc;
		}
		else
		{
			PutDbgStr(L"Message : send SOCKS connect command successfully!");
		}
		//Ӧ��  
		//VER   REP RSV     ATYP    BND.ADDR    BND.PORT  
		//1     1   X'00'   1       Variable    2  
		/*VER Э��汾: X��05��
		�� REP Ӧ���ֶ�:
		�� X��00�� �ɹ�
		�� X��01�� ��ͨ��SOCKS����������ʧ��
		�� X��02�� ���еĹ������������
		�� X��03�� ���粻�ɴ�
		�� X��04�� �������ɴ�
		�� X��05�� ���ӱ���
		�� X��06�� TTL��ʱ
		�� X��07�� ��֧�ֵ�����
		�� X��08�� ��֧�ֵĵ�ַ����
		�� X��09�� �C X��FF�� δ����
		�� RSV ����
		�� ATYP ����ĵ�ַ����
		�� IPV4��X��01��
		�� ������X��03��
		�� IPV6��X��04��
		�� BND.ADDR �������󶨵ĵ�ַ
		�� BND.PORT �������ֽ�˳���ʾ�ķ������󶨵Ķο�
		��ʶΪRSV���ֶα�����ΪX��00����*/
		if ((rc = recv(s, buf, 10, 0)) < 0) // �������������֮������ͽ��ղ���������Ϣ�ˣ�����  
		{
			PutDbgStr(L"Error %d : attempting to receive SOCKS connection reply!", WSAGetLastError());
			rc = ECONNREFUSED;
			return rc;
		}
		else
		{
			PutDbgStr(L"Message : receive SOCKS connection reply successfully!");
		}
		if (rc < 10)
		{
			PutDbgStr(L"Message : Short reply from SOCKS server!");
			return rc;
		}
		else
		{
			PutDbgStr(L"Message : reply from SOCKS larger than 10!");
		}
		//���Ӳ��ɹ�  
		if (buf[0] != 0x05)
		{
			PutDbgStr(L"Message : Socks V5 not supported!");
			return ECONNABORTED;
		}
		else
		{
			PutDbgStr(L"Message : Socks V5 is supported!");
		}
		if (buf[1] != 0x00)
		{
			PutDbgStr(L"Message : SOCKS connect failed!");
			switch ((int)buf[1])
			{
			case 1:
				PutDbgStr(L"General SOCKS server failure!");
				return ECONNABORTED;
			case 2:
				PutDbgStr(L"Connection denied by rule!");
				return ECONNABORTED;
			case 3:
				PutDbgStr(L"Network unreachable!");
				return ENETUNREACH;
			case 4:
				PutDbgStr(L"Host unreachable!");
				return EHOSTUNREACH;
			case 5:
				PutDbgStr(L"Connection refused!");
				return ECONNREFUSED;
			case 6:
				PutDbgStr(L"TTL Expired!");
				return ETIMEDOUT;
			case 7:
				PutDbgStr(L"Command not supported!");
				return ECONNABORTED;
			case 8:
				PutDbgStr(L"Address type not supported!");
				return ECONNABORTED;
			default:
				PutDbgStr(L"Unknown error!");
				return ECONNABORTED;
			}
		}
		else
		{
			PutDbgStr(L"Message : SOCKS connect Success!");
		}
	}
	else
	{
		PutDbgStr(L"Error : Method not supported! verstring[1]=%d", verstring[1]);
	}
	//�޸�socketΪ����������  
	nonBlock = 1;
	if (rc = ioctlsocket(s, FIONBIO, &nonBlock))
	{
		PutDbgStr(L"Error %d : Set Non-Blocking Failure!", WSAGetLastError());
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : Set Non-Blocking Successful!");
	}
	PutDbgStr(L"Message : Success!");
	return 0;
}

int ProxySendTo(SOCKET s, const struct sockaddr *name, int namelen, short port)
{
	int rc = 0;
	// ����Ӧ���ȱ�����socket������/���������ͣ����������������ֵ������ԭ�����ǲ�֪��������ȡ������  
	// �޸�socketΪ��������  
	if (rc = WSAEventSelect(s, 0, NULL))//��һ�����Բ���ִ��  
	{
		PutDbgStr(L"Error %d : WSAEventSelect-UDP Failure!", WSAGetLastError());
	}
	else
	{
		PutDbgStr(L"Message : WSAEventSelect-UDP successfully!");
	}
	unsigned long nonBlock = 0;
	if (rc = ioctlsocket(s, FIONBIO, &nonBlock))// ��������޸�Ϊ��������  
	{
		PutDbgStr(L"Error %d : Set Blocking-UDP Failure!", WSAGetLastError());
	}
	else
	{
		PutDbgStr(L"Message : Set Blocking-UDP successfully!");
	}
	//���Ӵ��������  
	sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;

	// !< TODO:�������
	inet_pton(AF_INET, "127.0.0.1", (void*)&serveraddr.sin_addr);
	serveraddr.sin_port = htons(10801); // �˿ں�  

	WSABUF DataBuf;
	char buffer[4];
	memset(buffer, 0, sizeof(buffer));
	DataBuf.len = 4;
	DataBuf.buf = buffer;
	int err = 0;
	if ((rc = NextProcTable.lpWSPConnect(s, (struct sockaddr *)&serveraddr, sizeof(struct sockaddr), &DataBuf, NULL, NULL, NULL, &err)) != 0)
	{
		PutDbgStr(L"Error %d : attempting to connect to SOCKS-UDP server!", err);
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : Connect to SOCKS-UDP server successfully!");
	}
	// !< ����UDP��͸����
	char abyUdpAssociateBuf[1024] = { 0 };
	const int SOCK5_PROXY_VERSION = 0x05;
	const int CMD_UDP_ASSOCIATE = 0x03;
	const int RESERVED = 0;
	const int IP_TYPE = 0x01;
	abyUdpAssociateBuf[0] = SOCK5_PROXY_VERSION;
	abyUdpAssociateBuf[1] = CMD_UDP_ASSOCIATE;
	abyUdpAssociateBuf[2] = RESERVED;
	abyUdpAssociateBuf[3] = IP_TYPE;
	int nAddr = inet_addr("127.0.0.1");
	short nPort = htons((short)port);
	memcpy(&abyUdpAssociateBuf[4], &nAddr, 4);
	memcpy(&abyUdpAssociateBuf[8], &nPort, 2);
	if ((rc = send(s, abyUdpAssociateBuf, 10, 0)) < 0)
	{
		PutDbgStr(L"Error %d : attempting to send SOCKS-UDP method negotiation!", WSAGetLastError());
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : send SOCKS-UDP method negotiation successfully!");
	}
	//���մ��������������Ϣ  
	//VER   METHOD  
	//1     1  
	/*��ǰ����ķ����У�
	�� X��00�� ����Ҫ��֤
	�� X��01�� GSSAPI
	�� X��02�� �û���/����
	�� X��03�� -- X��7F�� ��IANA����
	�� X��80�� -- X��FE�� Ϊ˽�˷�����������
	�� X��FF�� û�п��Խ��ܵķ���*/
	if ((rc = recv(s, verstring, 257, 0)) < 0)
	{
		PutDbgStr(L"Error %d : attempting to receive SOCKS-UDP method negotiation reply!", WSAGetLastError());
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : receive SOCKS-UDP method negotiation reply successfully!");
	}
	if (rc < 2)//����2�ֽ�  
	{
		PutDbgStr(L"Error : Short reply from SOCKS-UDP server!");
		rc = ECONNREFUSED;
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : reply from SOCKS-UDP server larger than 2");
	}
	// ���������ѡ�񷽷�  
	// �ж����ǵķ����Ƿ����  
	if (verstring[1] == 0xff)
	{
		PutDbgStr(L"Error : SOCKS-UDP server refused authentication methods!");
		rc = ECONNREFUSED;
		return rc;
	}
	else if (verstring[1] == 0x02)// ����2 �� �û���/����  
	{
		//���⴦��  
		PutDbgStr(L"Error : SOCKS-UDP server need username/password!");
	}
	else if (verstring[1] == 0x00)// ����0�� ����Ҫ��֤  
	{
		//����SOCKS����  
		//VER   CMD RSV     ATYP    DST.ADDR    DST.PROT  
		//1     1   X'00'   1       Variable    2  
		/* VER Э��汾: X��05��
		�� CMD
		�� CONNECT��X��01��
		�� BIND��X��02��
		�� UDP ASSOCIATE��X��03��
		�� RSV ����
		�� ATYP ����ĵ�ַ����
		�� IPV4��X��01��
		�� ������X��03��
		�� IPV6��X��04��'
		�� DST.ADDR Ŀ�ĵ�ַ
		�� DST.PORT �������ֽ�˳����ֵĶ˿ں�
		SOCKS�����������Դ��ַ��Ŀ�ĵ�ַ����������Ȼ������������ͷ���һ������Ӧ��*/
		struct sockaddr_in sin;
		sin = *(const struct sockaddr_in *)name;
		char buf[10];
		buf[0] = 0x05; // �汾 SOCKS5  
		buf[1] = 0x01; // ��������  
		buf[2] = 0x00; // �����ֶ�  
		buf[3] = 0x01; // IPV4  
		memcpy(&buf[4], &sin.sin_addr.S_un.S_addr, 4);
		memcpy(&buf[8], &sin.sin_port, 2);
		//����  
		if ((rc = send(s, buf, 10, 0)) < 0)
		{
			PutDbgStr(L"Error %d : attempting to send SOCKS-UDP connect command!", WSAGetLastError());
			return rc;
		}
		else
		{
			PutDbgStr(L"Message : send SOCKS-UDP connect command successfully!");
		}
		//Ӧ��  
		//VER   REP RSV     ATYP    BND.ADDR    BND.PORT  
		//1     1   X'00'   1       Variable    2  
		/*VER Э��汾: X��05��
		�� REP Ӧ���ֶ�:
		�� X��00�� �ɹ�
		�� X��01�� ��ͨ��SOCKS����������ʧ��
		�� X��02�� ���еĹ������������
		�� X��03�� ���粻�ɴ�
		�� X��04�� �������ɴ�
		�� X��05�� ���ӱ���
		�� X��06�� TTL��ʱ
		�� X��07�� ��֧�ֵ�����
		�� X��08�� ��֧�ֵĵ�ַ����
		�� X��09�� �C X��FF�� δ����
		�� RSV ����
		�� ATYP ����ĵ�ַ����
		�� IPV4��X��01��
		�� ������X��03��
		�� IPV6��X��04��
		�� BND.ADDR �������󶨵ĵ�ַ
		�� BND.PORT �������ֽ�˳���ʾ�ķ������󶨵Ķο�
		��ʶΪRSV���ֶα�����ΪX��00����*/
		if ((rc = recv(s, buf, 10, 0)) < 0) // �������������֮������ͽ��ղ���������Ϣ�ˣ�����  
		{
			PutDbgStr(L"Error %d : attempting to receive SOCKS-UDP connection reply!", WSAGetLastError());
			rc = ECONNREFUSED;
			return rc;
		}
		else
		{
			PutDbgStr(L"Message : receive SOCKS-UDP connection reply successfully!");
		}
		if (rc < 10)
		{
			PutDbgStr(L"Message : Short reply from SOCKS-UDP server!");
			return rc;
		}
		else
		{
			PutDbgStr(L"Message : reply from SOCKS-UDP larger than 10!");
		}
		//���Ӳ��ɹ�  
		if (buf[0] != 0x05)
		{
			PutDbgStr(L"Message : SOCKS-UDP V5 not supported!");
			return ECONNABORTED;
		}
		else
		{
			PutDbgStr(L"Message : SOCKS-UDP V5 is supported!");
		}
		if (buf[1] != 0x00)
		{
			PutDbgStr(L"Message : SOCKS-UDP connect failed!");
			switch ((int)buf[1])
			{
			case 1:
				PutDbgStr(L"General SOCKS-UDP server failure!");
				return ECONNABORTED;
			case 2:
				PutDbgStr(L"SOCKS-UDP Connection denied by rule!");
				return ECONNABORTED;
			case 3:
				PutDbgStr(L"SOCKS-UDP Network unreachable!");
				return ENETUNREACH;
			case 4:
				PutDbgStr(L"SOCKS-UDP Host unreachable!");
				return EHOSTUNREACH;
			case 5:
				PutDbgStr(L"SOCKS-UDP Connection refused!");
				return ECONNREFUSED;
			case 6:
				PutDbgStr(L"SOCKS-UDP TTL Expired!");
				return ETIMEDOUT;
			case 7:
				PutDbgStr(L"SOCKS-UDP Command not supported!");
				return ECONNABORTED;
			case 8:
				PutDbgStr(L"SOCKS-UDP Address type not supported!");
				return ECONNABORTED;
			default:
				PutDbgStr(L"SOCKS-UDP Unknown error!");
				return ECONNABORTED;
			}
		}
		else
		{
			PutDbgStr(L"Message : SOCKS-UDP connect Success!");
		}
	}
	else
	{
		PutDbgStr(L"Error : SOCKS-UDP Method not supported! verstring[1]=%d", verstring[1]);
	}
	//�޸�socketΪ����������  
	nonBlock = 1;
	if (rc = ioctlsocket(s, FIONBIO, &nonBlock))
	{
		PutDbgStr(L"Error %d : SOCKS-UDP Set Non-Blocking Failure!", WSAGetLastError());
		return rc;
	}
	else
	{
		PutDbgStr(L"Message : SOCKS-UDP Set Non-Blocking Successful!");
	}
	PutDbgStr(L"Message : SOCKS-UDP Success!");
	return 0;
}
//WSPConnect  
int WSPAPI WSPConnect(
	SOCKET s,
	const struct sockaddr *name,
	int namelen,
	LPWSABUF lpCallerData,
	LPWSABUF lpCalleeData,
	LPQOS lpSQOS,
	LPQOS lpGQOS,
	LPINT lpErrno)
{
	// !< TODO:��ӹ��˹���ȴ���
	TCHAR FullPath[MAX_PATH] = { 0x00 };
	TCHAR ProcessName[_MAX_FNAME] = { 0x00 };
	TCHAR drive[_MAX_DRIVE] = { 0x00 };
	TCHAR dir[_MAX_DIR] = { 0x00 };
	TCHAR ext[_MAX_EXT] = { 0x00 };
	GetModuleFileName(NULL, FullPath, MAX_PATH);
	_wsplitpath_s(FullPath, drive, dir, ProcessName, ext);
	PutDbgStr(L"WSPConnect Process:%ws", ProcessName);

	// !< 1.�������
	if (wcscmp(ProcessName, L"chrome") != 0)
	{
		return NextProcTable.lpWSPConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS, lpErrno);
	}

	// !< 2.Ŀ���ַ����
	char remoteip[64];
	inet_ntop(AF_INET, (void*)name, remoteip, ARRAYSIZE(remoteip));
	if (strcmp(remoteip, "127.0.0.1") == 0)
	{
	}

	return ProxyConnect(s, name, namelen);
}

//WSPSendTo  
int WINAPI WSPSendTo(
	__in   SOCKET s,
	__in   LPWSABUF lpBuffers,
	__in   DWORD dwBufferCount,
	__out  LPDWORD lpNumberOfBytesSent,
	__in   DWORD dwFlags,
	__in   const struct sockaddr *lpTo,
	__in   int iTolen,
	__in   LPWSAOVERLAPPED lpOverlapped,
	__in   LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	__in   LPWSATHREADID lpThreadId,
	__out  LPINT lpErrno
)
{
	// !< TODO:��ӹ��˹���ȴ���
	TCHAR FullPath[MAX_PATH] = { 0x00 };
	TCHAR ProcessName[_MAX_FNAME] = { 0x00 };
	TCHAR drive[_MAX_DRIVE] = { 0x00 };
	TCHAR dir[_MAX_DIR] = { 0x00 };
	TCHAR ext[_MAX_EXT] = { 0x00 };
	GetModuleFileName(NULL, FullPath, MAX_PATH);
	_wsplitpath_s(FullPath, drive, dir, ProcessName, ext);
	PutDbgStr(L"WSPSendTo Process:%ws", ProcessName);

	// !< 1.�������
	if (wcscmp(ProcessName, L"chrome") != 0)
	{
		return NextProcTable.lpWSPSendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped, lpCompletionRoutine, lpThreadId, lpErrno);
	}

	// !< 2.Ŀ���ַ����
	char remoteip[64];
	inet_ntop(AF_INET, (void*)lpTo, remoteip, ARRAYSIZE(remoteip));
	if (strcmp(remoteip, "127.0.0.1") == 0)
	{
	}

	return socksProxy(s, lpTo, iTolen);
}

//WSPSocket  
SOCKET WINAPI WSPSocket(
	__in   int af,
	__in   int type,
	__in   int protocol,
	__in   LPWSAPROTOCOL_INFO lpProtocolInfo,
	__in   GROUP g,
	DWORD dwFlags,
	__out  LPINT lpErrno
)
{
	return NextProcTable.lpWSPSocket(af, type, protocol, lpProtocolInfo, g, dwFlags, lpErrno);
}
//WSPBind  
int WINAPI WSPBind(
	__in   SOCKET s,
	__in   const struct sockaddr *name,
	__in   int namelen,
	__out  LPINT lpErrno
)
{
	return NextProcTable.lpWSPBind(s, name, namelen, lpErrno);
}
//WSPSend  
int WINAPI WSPSend(
	__in   SOCKET s,
	__in   LPWSABUF lpBuffers,
	__in   DWORD dwBufferCount,
	__out  LPDWORD lpNumberOfBytesSent,
	__in   DWORD dwFlags,
	__in   LPWSAOVERLAPPED lpOverlapped,
	__in   LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	__in   LPWSATHREADID lpThreadId,
	__out  LPINT lpErrno
)
{
	return NextProcTable.lpWSPSend(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine, lpThreadId, lpErrno);
}
//WSPRecv  
int WINAPI WSPRecv(
	__in     SOCKET s,
	__inout  LPWSABUF lpBuffers,
	__in     DWORD dwBufferCount,
	__out    LPDWORD lpNumberOfBytesRecvd,
	__inout  LPDWORD lpFlags,
	__in     LPWSAOVERLAPPED lpOverlapped,
	__in     LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	__in     LPWSATHREADID lpThreadId,
	__out    LPINT lpErrno
)
{
	return NextProcTable.lpWSPRecv(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine, lpThreadId, lpErrno);
}
//WSPRecvFrom  
int WINAPI WSPRecvFrom(
	__in     SOCKET s,
	__inout  LPWSABUF lpBuffers,
	__in     DWORD dwBufferCount,
	__out    LPDWORD lpNumberOfBytesRecvd,
	__inout  LPDWORD lpFlags,
	__out    struct sockaddr *lpFrom,
	__inout  LPINT lpFromlen,
	__in     LPWSAOVERLAPPED lpOverlapped,
	__in     LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	__in     LPWSATHREADID lpThreadId,
	__inout  LPINT lpErrno
)
{
	return NextProcTable.lpWSPRecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine, lpThreadId, lpErrno);
}
//WSPStartup  
int WSPAPI WSPStartup(
	WORD wversionrequested,
	LPWSPDATA         lpwspdata,
	LPWSAPROTOCOL_INFOW lpProtoInfo,
	WSPUPCALLTABLE upcalltable,
	LPWSPPROC_TABLE lpproctable
)
{
	PutDbgStr(L"LSP-Proxy WSPStartup ...");
	int           i;
	int           errorcode;
	int           filterpathlen;
	DWORD         layerid = 0;
	DWORD         nextlayerid = 0;
	TCHAR         *filterpath;
	HINSTANCE     hfilter;
	LPWSPSTARTUP  wspstartupfunc = NULL;
	if (lpProtoInfo->ProtocolChain.ChainLen <= 1)
	{
		PutDbgStr(L"ChainLen<=1");
		return FALSE;
	}
	GetLSP();
	for (i = 0; i < TotalProtos; i++)
	{
		if (memcmp(&ProtoInfo[i].ProviderId, &ProviderGuid, sizeof(GUID)) == 0)
		{
			layerid = ProtoInfo[i].dwCatalogEntryId;
			break;
		}
	}
	for (i = 0; i < lpProtoInfo->ProtocolChain.ChainLen; i++)
	{
		if (lpProtoInfo->ProtocolChain.ChainEntries[i] == layerid)
		{
			nextlayerid = lpProtoInfo->ProtocolChain.ChainEntries[i + 1];
			break;
		}
	}
	filterpathlen = MAX_PATH;
	filterpath = (TCHAR*)GlobalAlloc(GPTR, filterpathlen);
	for (i = 0; i < TotalProtos; i++)
	{
		if (nextlayerid == ProtoInfo[i].dwCatalogEntryId)
		{
			if (WSCGetProviderPath(&ProtoInfo[i].ProviderId, filterpath, &filterpathlen, &errorcode) == SOCKET_ERROR)
			{
				PutDbgStr(L"WSCGetProviderPath Error!");
				return WSAEPROVIDERFAILEDINIT;
			}
			break;
		}
	}
	if (!ExpandEnvironmentStrings(filterpath, filterpath, MAX_PATH))
	{
		PutDbgStr(L"ExpandEnvironmentStrings Error!");
		return WSAEPROVIDERFAILEDINIT;
	}
	if ((hfilter = LoadLibrary(filterpath)) == NULL)
	{
		PutDbgStr(L"LoadLibrary Error! TotalProtos=%d filterpath=%ws layer=%d, nextlayer=%d", TotalProtos, filterpath, layerid, nextlayerid);
		return WSAEPROVIDERFAILEDINIT;
	}
	if ((wspstartupfunc = (LPWSPSTARTUP)GetProcAddress(hfilter, "WSPStartup")) == NULL)
	{
		PutDbgStr(L"GetProcessAddress Error!");
		return WSAEPROVIDERFAILEDINIT;
	}
	if ((errorcode = wspstartupfunc(wversionrequested, lpwspdata, lpProtoInfo, upcalltable, lpproctable)) != ERROR_SUCCESS)
	{
		PutDbgStr(L"wspstartupfunc Error!");
		return errorcode;
	}
	NextProcTable = *lpproctable;// ����ԭ������ں�����  
								 //��д����  
	lpproctable->lpWSPSendTo = WSPSendTo;
	lpproctable->lpWSPSend = WSPSend;
	lpproctable->lpWSPBind = WSPBind;
	lpproctable->lpWSPConnect = WSPConnect;
	lpproctable->lpWSPRecv = WSPRecv;
	lpproctable->lpWSPRecvFrom = WSPRecvFrom;
	lpproctable->lpWSPSocket = WSPSocket;
	FreeLSP();
	return 0;
}

// DLL��ں���  
BOOL APIENTRY DllMain(HMODULE /* hModule */, DWORD ul_reason_for_call, LPVOID /* lpReserved */)
{
	TCHAR   processname[MAX_PATH];
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		GetModuleFileName(NULL, processname, MAX_PATH);
		PutDbgStr(L"%s Loading IPFilter ...", processname);
	}
	return TRUE;
}