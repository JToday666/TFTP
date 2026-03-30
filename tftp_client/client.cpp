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
	SOCKET udpsocket = socket(AF_INET, SOCK_DGRAM, 0);		//参数：协议族ipv4，数据报格式
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
	datalen = len + 2 + 1 + type + 1;  //datalen是一个引用变量，通过这个变量将数据长度传递出去
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
	ack[1] = 0x04;			  //ACK数据包的编号是0x04
	no = htons(no);			  //将主机的字节序转换为网络字节序，由于不同操作系统的存储方式不同，为了方便交流，规定了统一的网络传输字节序，由对应的主机根据自身系统的存储规则转换为主机字节序
	memcpy(ack + 2, &no, 2);
	no = ntohs(no);
	return ack;			  //ntohs函数与htons相反，将网络字节序转换为主机字节序，这里是因为no是引用类型的变量，所以必须要在传输完后恢复到在主机上存储的格式
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

int main()
{
	FILE* fp = fopen("TFTP_client.log", "a");  //打开日志文件
	char commonbuf[2048];
	int buflen;
	int Numbertokill;		//numbertoki11表示一个数据包recvfrom超时的次数
	int Killtime;			//killtime表示sendto超时的次数，与recvfrom超时分开计算
	clock_t start, end;		//分别记录着传输的开始时间和结束时间，用于计算传输速率
	double runtime;
	SOCKET sock = getUdpSocket();
	sockaddr_in addr;
	int recvTimeout = 1000;	//代表1000ms，即1s
	int sendTimeout = 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendTimeout, sizeof(int));

	while (1)
	{
		printf("1. 上传文件\t2.下载文件\t0.关闭TFTP客户端\n");
		int choice;
		scanf("%d", &choice);

		//上传文件
		if (choice == 1)
		{
			/***********************更改服务器IP***********************/
			addr = getAddr("10.11.71.245", 69);		//第一个数据包(WRQ/RRQ)总是发送向69端口
			/**********************************************************/
			printf("请输入要上传文件的全名:\n");
			char name[1000];
			int type;
			scanf("%s", name);
			printf("请选择上传文件的方式:1.netascii 2.octet\n");
			scanf("%d", &type);
			if (type == 1)
				type = 8;
			else
				type = 5;
			int datalen;
			char* sendData = RequestUploadPack(name, datalen, type);
			buflen = datalen;
			Numbertokill = 1;
			memcpy(commonbuf, sendData, datalen);		//commonbuf是一个专门用于数据重传的缓冲区。有可能被重传的数据都会统一的放进commonbuf中，重传机制会直接从commonbuf中获得数据
			int res = sendto(sock, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));		//第一次发送WRQ包
			start = clock();		//开始计时
			print_time(fp);
			fprintf(fp, "send WRQ for file: %s\n", name);
			Killtime = 1;
			while (res != datalen) {		//如果sendto函数失败了，则立即重新sendto，在sendto成功或者达到上限次数之前不会进
				std::cout << "send WRQ failed: " << Killtime << "times" << std::endl;
				//规定sendto失败十次就自动结束传输
				if (Killtime <= 10) {
					res = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
					Killtime++;
				}
				else
					break;
			}
			if (Killtime > 10)			//传输失败
				continue;
			delete[] sendData;

			FILE* f = fopen(name, "rb");
			if (f == NULL) {
				std::cout << "File" << name << "open failed!" << std::endl;
				continue;
			}
			short block = 0;
			datalen = 0;
			int RST = 0;	//记录重传次数
			int Fullsize = 0;	//记录文件的总大小
			while (1) {		//开始传输
				char buf[1024];
				sockaddr_in server;		//从server反馈的数据包中获得其分配的端口号信息
				int len = sizeof(server);
				res = recvfrom(sock, buf, 1024, 0, (sockaddr*)&server, &len); //监听服务器的数据包

				//如果没有收到数据
				if (res == -1) {
					printf("%d ", Numbertokill);
					if (Numbertokill > 10) {		//如果连续十次没有收到回应
						printf("No acks get.transmission failed\n");
						print_time(fp);
						fprintf(fp, "Upload file:%s failed.\n", name);//打印错误提示并结束传输
						break;
					}
					//重传上一个数据包
					int res = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
					RST++;
					std::cout << "resend last blk" << std::endl;
					Killtime = 1;		//同上处理sendto超时的情况
					while (res != buflen) {
						std::cout << "resend last blk failed:" << Killtime << "times" << std::endl;
						if (Killtime <= 10) {
							res = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
							Killtime++;
						}
						else
							break;
					}
					if (Killtime > 10)
						break;
					Numbertokill++;
				}
				//收到数据
				if (res > 0) {
					short flag;
					memcpy(&flag, buf, 2);		//获取数据包的类型编号
					flag = ntohs(flag);
					//ACK
					if (flag == 4) {
						short no;		//进一步拆包获取ACK包内的序列号
						memcpy(&no, buf + 2, 2);
						no = ntohs(no);		//将序列号从网络字节序转化为主机字节序
						if (no == block) {		//如果ACK的序列号是客户端最近发送的数据包的序号
							addr = server;
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
							Fullsize += datalen - 4;				//fullsize要去除数据包中头部的长度
							Numbertokill = 1;						//重置当前数据包的重发次数
							memcpy(commonbuf, sendData, datalen);	//更新commonbuf中的内容，准备下一次可能的重传
							if (sendData == NULL) {		//如果在构造数据包的过程中失败了
								std::cout << "File reading mistakes!" << std::endl;
								break;
							}
							int res = sendto(sock, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));		//将刚刚构造的DATA包发送出去
							Killtime = 1;
							while (res != datalen) {
								std::cout << "send block " << block << "failed: " << Killtime << "times" << std::endl;
								if (Killtime <= 10) {
									res = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
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
			}
			fclose(f);
		}

		//下载文件
		if (choice == 2) {
			addr = getAddr("127.0.0.1", 69);
			printf("请输入要下载文件的全名:\n");
			char name[1000];
			int type;
			scanf("%s", name);
			printf("请选择下载文件的方式:1.netascii 2.octet\n");
			scanf("%d", &type);
			if (type == 1)
				type = 8;
			else
				type = 5;
			int datalen;
			char* sendData = RequestDownloadPack(name, datalen, type);
			buflen = datalen;
			Numbertokill = 1;
			memcpy(commonbuf, sendData, datalen);
			int res = sendto(sock, sendData, datalen, 0, (sockaddr*)&addr, sizeof(addr));
			start = clock();
			print_time(fp);
			fprintf(fp, "send RRQ for file: %s\n", name);
			Killtime = 1;
			while (res != datalen) {
				std::cout << "send RRQ failed:" << Killtime << "times" << std::endl;
				if (Killtime <= 10) {
					res = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
					Killtime++;
				}
				else
					break;
			}
			if (Killtime > 10)
				continue;
			delete[] sendData;

			FILE* f = fopen(name, "wb");
			if (f == NULL) {
				std::cout << "File" << name << "open failed!" << std::endl;
				continue;
			}
			int want_recv = 1;
			int RST = 0;
			int Fullsize = 0;
			while (1) {
				char buf[1024];
				sockaddr_in server;
				int len = sizeof(server);
				res = recvfrom(sock, buf, 1024, 0, (sockaddr*)&server, &len);

				if (res == -1) {
					if (Numbertokill > 10) {
						printf("No block get.transmission failed\n");
						print_time(fp);
						fprintf(fp, "Download file:%s failed.\n", name);
						break;
					}
					int res = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
					RST++;
					std::cout << "resend last blk" << std::endl;
					Killtime = 1;
					while (res != buflen) {
						std::cout << "resend last blk failed:" << Killtime << "times" << std::endl;
						if (Killtime <= 10) {
							res = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&addr, sizeof(addr));
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
						addr = server;
						short no;
						memcpy(&no, buf + 2, 2);
						no = ntohs(no);
						std::cout << "Pack No=" << no << std::endl;
						char* ack = AckPack(no);		//对该数据包制作ACK(并不关注是否是一个新数据包)
						int sendlen = sendto(sock, ack, 4, 0, (sockaddr*)&addr, sizeof(addr));
						Killtime = 1;
						while (sendlen != 4) {		//发送长度不为4说明ACK包sendto出现了错误
							std::cout << "resend last ack failed: " << Killtime << "times" << std::endl;
							if (Killtime <= 10) {
								sendlen = sendto(sock, ack, 4, 0, (sockaddr*)&addr, sizeof(addr));
								Killtime++;
							}
							else
								break;
						}
						if (Killtime > 10)
							break;

						if (no == want_recv) {		//want_recv变量维护用户当前期望收到的下一个数据包的编号如果收到的数据包是用户期望收到的数据包
							buflen = 4;
							Numbertokill = 1;
							memcpy(commonbuf, ack, 4);//更新commonbuf的内容，当超时发生时，客户端将会不断重传对当前收到的编号最大的一个(而非最近收到的一个)DATA的ACK
							fwrite(buf + 4, res - 4, 1, f);
							Fullsize += res - 4;
							if (res - 4 >= 0 && res - 4 < 512) {		//如果当前数据包的大小小于512bytes，说明传输完毕
								std::cout << "download finished!" << std::endl;
								end = clock();
								runtime = (double)(end - start) / CLOCKS_PER_SEC;		//计算时间
								print_time(fp);
								printf("Average transmission rate: %.2lf kb/s\n", Fullsize / runtime / 1000);
								fprintf(fp, "Download file: %s finished,resent times: %d;Fullsize: %d\n", name, RST, Fullsize);
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
		if (choice == 0)
			break;
	}
	closesocket(sock);
	fprintf(fp, "\n");
	fclose(fp);
	return 0;
}