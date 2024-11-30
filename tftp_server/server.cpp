#define _CRT_SECURE_NO_WARNINGS 0
#define _WINSOCK_DEPRECATED_NO_WARNINGS 0

#include <cstdlib>
#include <iostream>
#include <time.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
using namespace std;

//初始化UDPsocket
SOCKET getUdpSocket()
{
	WORD ver = MAKEWORD(2, 2);
	WSADATA lpData;
	int err = WSAStartup(ver, &lpData);
	if (err != 0)
		return -1;
	SOCKET udpsocket = socket(AF_INET, SOCK_DGRAM, 0);		//ipv4，数据报格式
	if (udpsocket == INVALID_SOCKET)
		return -2;
	return udpsocket;
}

//根据输入的IP地址和端口号构造出一个存储着地址的sockaddr_in类型
sockaddr_in getAddr(const char* ip, int port)
{
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.S_un.S_addr = inet_addr(ip);
	return addr;
}

//构造RRQ数据包
char* RequestDownloadPack(char* content, int& datalen, int type)
{
	int len = strlen(content);
	char* buf = new char[len + 2 + 2 + type];
	buf[0] = 0x00;
	buf[1] = 0x01;	//RRQ在TFTP中用0x01表示
	memcpy(buf + 2, content, len);	//将被请求的文件名放入RRQ数据包
	memcpy(buf + 2 + len, "\0", 1);
	if (type == 5)	//根据用户规定的传输格式来构造数据包
		memcpy(buf + 2 + len + 1, "octet", 5);
	else
		memcpy(buf + 2 + len + 1, "netascii", 8);
	memcpy(buf + 2 + len + 1 + type, "\0", 1);
	datalen = len + 2 + 1 + type + 1;  //datalen将数据长度传递出去
	return buf;
}

//构造WRQ数据包
char* RequestUploadPack(char* content, int& datalen, int type)
{
	int len = strlen(content);
	char* buf = new char[len + 2 + 2 + type];  //开辟一个大小正好合适的空间存放WRQ
	buf[0] = 0x00;
	buf[1] = 0x02;			 //WRQ的编号是0x02
	memcpy(buf + 2, content, len);
	memcpy(buf + 2 + len, "\0", 1);
	if (type == 5)
		memcpy(buf + 2 + len + 1, "octet", 5);
	else
		memcpy(buf + 2 + len + 1, "netascii", 8);
	memcpy(buf + 2 + len + 1 + type, "\0", 1);
	datalen = len + 2 + 1 + type + 1;
	return buf;
}

//制作ACK数据包
char* AckPack(short& no)
{
	char* ack = new char[4];
	ack[0] = 0x00;
	ack[1] = 0x04;				//ACK数据包的编号是0x04
	no = htons(no);				//将主机的字节序转换为网络字节序
	memcpy(ack + 2, &no, 2);
	no = ntohs(no);				//将网络字节序转换为主机字节序
	return ack;
}

//制作DATA数据包
char* MakeData(short& no, FILE* f, int& datalen)
{
	char temp[512];
	int sum = fread(temp, 1, 512, f);//读入512个字节的文件内容
	if (!ferror(f))
	{
		char* buf = new char[4 + sum];
		buf[0] = 0x00;
		buf[1] = 0x03;//DATA数据包的编号是0x03
		no = htons(no);
		memcpy(buf + 2, &no, 2);
		no = ntohs(no);
		memcpy(buf + 4, temp, sum);
		datalen = sum + 4;//通过引用变量datalen来传递数据包长度
		return buf;
	}
	else
		return NULL;
}

//制作ERROR数据包
char* ErrorPack(short errorcode, int& datalen)
{
	char errormessage[50] = "Unknown error";
	switch (errorcode)
	{
	case 1:
		strcpy(errormessage, "File not found");
		break;
	case 2:
		strcpy(errormessage, "Access violation");
		break;
	case 3:
		strcpy(errormessage, "Disk full or allocation exceeded");
		break;
	case 4:
		strcpy(errormessage, "Illegal TFTP operation");
		break;
	case 5:
		strcpy(errormessage, "Unknown transfer ID");
		break;
	case 6:
		strcpy(errormessage, "File already exists");
		break;
	case 7:
		strcpy(errormessage, "No such user");
		break;
	}
	int len = strlen(errormessage);
	char* buf = new char[4 + len + 1];
		buf[0] = 0x00;
		buf[1] = 0x05;
		errorcode = htons(errorcode);
		memcpy(buf + 2, &errorcode, 2);
	memcpy(buf + 4, errormessage, len + 1);
	datalen = 4 + len + 1;
	//if (fp != NULL) {
	//	print_time(fp);
	//	fprintf(fp, "Error %d: %s\n", errorcode, errormessage);
	//}
		return buf;
	}

//按照“年-月-日-时-分-秒”的格式输出当前时间，用于构造日志文件
void print_time(FILE* fp)
{
	time_t t;
	time(&t);		//获取当前时间
	char stime[100];
	strcpy(stime, ctime(&t));		//ctime将time_t类型转化为人类易读的时间格式
	*(strchr(stime, '\n')) = '\0';		//转化结果最后包含一个回车符，将该回车符去掉
	fprintf(fp, " [ %s ] ", stime);
	return;
}

void* handleClient(sockaddr_in client, int len, char* request)
{
	short op;
	char name[1000], type[15];
	memcpy(&op, request, 2);
	op = ntohs(op);
	strcpy(name, request + 2);
	strcpy(type, request + 2 + strlen(name) + 1);
	
	FILE* fp = fopen("TFTP_server.log", "a");
	char commonbuf[2048];	//缓存区
	int buflen = 0;			//缓存长度
	int Numbertokill;		//numbertokill表示一个数据包recvfrom超时的次数
	int Killtime;			//killtime表示sendto超时的次数，与recvfrom超时分开计算
	int res, sendlen;
	clock_t start, end;		//传输的开始时间和结束时间，用于计算传输速率
	double runtime;
	SOCKET sock = getUdpSocket();
	int recvTimeout = 1000;	//代表1000ms，即1s
	int sendTimeout = 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendTimeout, sizeof(int));

	//下载请求，上传文件
	if (op == 1) {
		printf("RRQ下载\n");
		print_time(fp);
		fprintf(fp, "receive RRQ for file: %s , datatype: %s\n", name, type);

		FILE* f = fopen(name, "rb");
		//打开失败
		if (f == NULL) {
			std::cout << "File " << name << " open failed!" << std::endl;
			fprintf(fp, "File not found or open failed\n");
			int datalen = 0;
			char* errdata = ErrorPack(1, datalen);
			sendlen = sendto(sock, errdata, datalen, 0, (sockaddr*)&client, sizeof(client));
			Killtime = 1;
			while (sendlen != datalen) {
				if (Killtime <= 10) {
					sendlen = sendto(sock, errdata, buflen, 0, (sockaddr*)&client, sizeof(client));
					Killtime++;
				}
				else
					break;
			}
			if (Killtime > 10)
				fprintf(fp, "error pack send failed\n");
			return NULL;
		}
		//开始传输
		start = clock();
		char* sendData;
		int datalen = 0;
		short block = 0;
		int RST = 0;		//记录重传次数
		int Fullsize = 0;	//记录文件的总大小
		short no = 0;
		res = 1;
		Numbertokill = 1;
		int resend = 1;
		char buf[1024];
		char* ack = AckPack(no);
		memcpy(buf, ack, 4);
		
		while (1) {
			//如果没有收到数据
			if (res == -1) {
				if (Numbertokill > 10) {
					printf("No acks get.transmission failed\n");
					print_time(fp);
					fprintf(fp, "Upload file: %s failed.\n", name);//打印错误提示并结束传输
					break;
				}
				//重传上一个数据包
				sendlen = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&client, sizeof(client));
				RST++;
				std::cout << "resend last blk" << std::endl;
				Killtime = 1;		//同上处理sendto超时的情况
				while (sendlen != buflen) {
					std::cout << "resend last blk failed:" << Killtime << "times" << std::endl;
					if (Killtime <= 10) {
						sendlen = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&client, sizeof(client));
						Killtime++;
					}
					else
						break;
				}
				if (Killtime > 10)
					printf("last blk send failed\n");
					break;

				Numbertokill++;
			}
			//收到数据
			if (res > 0) {
				short flag;
				memcpy(&flag, buf, 2);
				flag = ntohs(flag);
				//ACK
				if (flag == 4) {
					memcpy(&no, buf + 2, 2);
					no = ntohs(no);
					if (no == block) {
						if (feof(f) && datalen != 516) { //如果上传文件已经全部上传完毕
							std::cout << "upload finished!" << std::endl; //结束传输
							end = clock();
							runtime = (double)(end - start) / CLOCKS_PER_SEC; //计算传输时间
							print_time(fp);
							printf("Average transmission rate: %.2lf kb/s\n", Fullsize / runtime / 1000);
							fprintf(fp, "Upload file: %s finished. resent times: %d. Fullsize: %d\n", name, RST, Fullsize);
							break;
						}

						block++;		//否则，制作下一个DATA包
						sendData = MakeData(block, f, datalen);
						buflen = datalen;
						memcpy(commonbuf, sendData, datalen);		//更新缓存区
						Fullsize += datalen - 4;
						Numbertokill = 1;			//重置当前数据包的重发次数
						if (sendData == NULL) {		//构造数据包失败
							std::cout << "File reading mistakes!" << std::endl;
							break;
						}
						sendlen = sendto(sock, sendData, datalen, 0, (sockaddr*)&client, sizeof(client));		//将刚刚构造的DATA包发送出去
						Killtime = 1;
						while (sendlen != datalen) {
							std::cout << "send block " << block << "failed: " << Killtime << "times" << std::endl;
							if (Killtime <= 10) {
								sendlen = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&client, sizeof(client));
								Killtime++;
							}
							else
								break;
						}
						if (Killtime > 10)
							continue;
						std::cout << "Pack No=" << block << std::endl;		//向用户输出当前正在传输的数据包的编号
					}
				}
				//ERROR
				if (flag == 5) {
					short errorcode;
					memcpy(&errorcode, buf + 2, 2);		//拆解ERROR包并获得错误码
					errorcode = ntohs(errorcode);
					char strError[1024];		//继续拆解并获得错误详细信息
					int iter = 0;
					while (*(buf + iter + 4) += 0) {
						memcpy(strError + iter, buf + iter + 4, 1);
						++iter;
					}
					*(strError + iter + 1) = '\0';
					std::cout << "Error" << errorcode << " " << strError << std::endl;		//输出错误信息
					print_time(fp);
					fprintf(fp, "Error %d %s\n", errorcode, strError);		//向日志文件输出错误信息
					break;		//结束传输
				}
			}
			res = recvfrom(sock, buf, 1024, 0, (sockaddr*)&client, &len);
		}

	}
	//上传请求，接收文件
	else if (op == 2) {
		printf("WRQ上传\n");
		print_time(fp);
		fprintf(fp, "receive WRQ for file: %s , datatype: %s\n", name, type);

		FILE* f = fopen(name, "wb");
		if (f == NULL) {
			std::cout << "File" << name << "open failed!" << std::endl;
			return NULL;
		}
		start = clock();
		short no = 0;
		char* ack = AckPack(no);
		memcpy(commonbuf, ack, 4);
		sendlen = sendto(sock, ack, 4, 0, (sockaddr*)&client, sizeof(client));
		Killtime = 1;
		while (sendlen != 4) {		//发送长度不为4说明ACK包sendto出现了错误
			std::cout << "resend last ack failed: " << Killtime << "times" << std::endl;
			if (Killtime <= 10) {
				sendlen = sendto(sock, ack, 4, 0, (sockaddr*)&client, sizeof(client));
				Killtime++;
			}
			else
				break;
		}
		if (Killtime > 10) {
			print_time(fp);
			fprintf(fp, "connect failed.\n");
		}

		//开始接收
		int want_recv = 1;
		int RST = 0;
		int Fullsize = 0;
		Numbertokill = 1;
		int finish = 0;
		while (1) {
			char buf[1024];
			res = recvfrom(sock, buf, 1024, 0, (sockaddr*)&client, &len);
			if (res == -1) {
				if (Numbertokill > 10) {
					printf("No block get.transmission failed\n");
					print_time(fp);
					fprintf(fp, "Download file: %s failed.\n", name);
					break;
				}
				sendlen = sendto(sock, commonbuf, 4, 0, (sockaddr*)&client, sizeof(client));
				RST++;
				std::cout << "resend last blk" << std::endl;
				Killtime = 1;
				while (sendlen != 4) {
					std::cout << "resend last blk failed:" << Killtime << "times" << std::endl;
					if (Killtime <= 10) {
						sendlen = sendto(sock, commonbuf, 4, 0, (sockaddr*)&client, sizeof(client));
						Killtime++;
					}
					else
						break;
				}
				if (Killtime > 10)
					break;
				Numbertokill++;
			}
			if (res > 0) {
				short flag;
				memcpy(&flag, buf, 2);
				flag = ntohs(flag);
				//DATA
				if (flag == 3) {
					memcpy(&no, buf + 2, 2);
					no = ntohs(no);
					std::cout << "Pack No=" << no << std::endl;
					ack = AckPack(no);		//对该数据包制作ACK(并不关注是否是一个新数据包)
					sendlen = sendto(sock, ack, 4, 0, (sockaddr*)&client, sizeof(client));
					Killtime = 1;
					while (sendlen != 4) {		//发送长度不为4说明ACK包sendto出现了错误
						std::cout << "resend last ack failed: " << Killtime << "times" << std::endl;
						if (Killtime <= 10) {
							sendlen = sendto(sock, ack, 4, 0, (sockaddr*)&client, sizeof(client));
							Killtime++;
						}
						else
							break;
					}
					if (Killtime > 10)
						break;

					if (no == want_recv) {		//want recv变量维护用户当前期望收到的下一个数据包的编号
						Numbertokill = 1;
						memcpy(commonbuf, ack, 4);//更新commonbuf的内容，当超时发生时，客户端将会不断重传对当前收到的编号最大的一个(而非最近收到的一个)DATA的ACK
						fwrite(buf + 4, res - 4, 1, f);
						Fullsize += res - 4;
						if (res - 4 >= 0 && res - 4 < 512) {		//如果当前数据包的大小小于512bytes，说明传输完毕
							std::cout << "download finished!" << std::endl;
							finish = 1;
							recvTimeout = 5000;
							setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));

							end = clock();
							runtime = (double)(end - start) / CLOCKS_PER_SEC;		//计算时间
							print_time(fp);
							printf("Average transmission rate: %.2lf kb/s\n", Fullsize / runtime / 1000);
							fprintf(fp, "Download file: %s finished,resent times: %d;Fullsize: %d\n", name, RST, Fullsize);

							while (-1 != recvfrom(sock, buf, 1024, 0, (sockaddr*)&client, &len)) {
								sendlen = sendto(sock, commonbuf, 4, 0, (sockaddr*)&client, sizeof(client));
								std::cout << "resend last blk" << std::endl;
								Killtime = 1;
								while (sendlen != 4) {
									std::cout << "resend last blk failed:" << Killtime << "times" << std::endl;
									if (Killtime <= 10) {
										sendlen = sendto(sock, commonbuf, 4, 0, (sockaddr*)&client, sizeof(client));
										Killtime++;
									}
									else
										break;
								}
								if (Killtime > 10)
									break;
							}
							recvTimeout = 1000;
							setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));
							printf("5seconds no new block get. connect close\n");
							break;
						}
						want_recv++;
					}
				}
				//ERROR
				if (flag == 5) {
					short errorcode;
					memcpy(&errorcode, buf + 2, 2);		//拆解ERROR包并获得错误码
					errorcode = ntohs(errorcode);
					char strError[1024];		//继续拆解并获得错误详细信息
					int iter = 0;
					while (*(buf + iter + 4) += 0) {
						memcpy(strError + iter, buf + iter + 4, 1);
						++iter;
					}
					*(strError + iter + 1) = '\0';
					std::cout << "Error" << errorcode << " " << strError << std::endl;		//输出错误信息
					print_time(fp);
					fprintf(fp, "Error %d %s\n", errorcode, strError);		//向日志文件输出错误信息
					break;		//结束传输
				}
			}
		}
		fclose(f);
	}
	else {
		printf("request error");
		return NULL;
	}
	closesocket(sock);
	fprintf(fp, "\n");
	fclose(fp);

	return NULL;
}

int main() {
	SOCKET listen_sock = getUdpSocket();
	sockaddr_in addr;

	/***********************更改本机IP***********************/
	addr = getAddr("10.21.52.25", 69);
	/********************************************************/

	if (-1 == (bind(listen_sock, (LPSOCKADDR)&addr, sizeof(addr))))
	{
		perror("Server Bind Failed:");
		exit(1);
	}

	std::cout << "TFTP Server is running..." << std::endl;

	//int recvTimeout = 1000;
	//int sendTimeout = 1000;
	//setsockopt(listen_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));
	//setsockopt(listen_sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendTimeout, sizeof(int));

	char request[1024];
	sockaddr_in client;
	int len = sizeof(client);

	//多线程
	while (1) {
		int res = recvfrom(listen_sock, request, 1024, 0, (sockaddr*)&client, &len);
		if (res > 0)
			handleClient(client, len, request);
	}
	closesocket(listen_sock);
	WSACleanup();
	return 0;
}