#define _CRT_SECURE_NO_WARNINGS 0
#define _WINSOCK_DEPRECATED_NO_WARNINGS 0

#include <cstdlib>
#include <iostream>
#include <time.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
using namespace std;

//��ʼ��UDPsocket
SOCKET getUdpSocket()
{
	WORD ver = MAKEWORD(2, 2);
	WSADATA lpData;
	int err = WSAStartup(ver, &lpData);
	if (err != 0)
		return -1;
	SOCKET udpsocket = socket(AF_INET, SOCK_DGRAM, 0);		//ipv4�����ݱ���ʽ
	if (udpsocket == INVALID_SOCKET)
		return -2;
	return udpsocket;
}

//���������IP��ַ�Ͷ˿ںŹ����һ���洢�ŵ�ַ��sockaddr_in����
sockaddr_in getAddr(const char* ip, int port)
{
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.S_un.S_addr = inet_addr(ip);
	return addr;
}

//����RRQ���ݰ�
char* RequestDownloadPack(char* content, int& datalen, int type)
{
	int len = strlen(content);
	char* buf = new char[len + 2 + 2 + type];
	buf[0] = 0x00;
	buf[1] = 0x01;	//RRQ��TFTP����0x01��ʾ
	memcpy(buf + 2, content, len);	//����������ļ�������RRQ���ݰ�
	memcpy(buf + 2 + len, "\0", 1);
	if (type == 5)	//�����û��涨�Ĵ����ʽ���������ݰ�
		memcpy(buf + 2 + len + 1, "octet", 5);
	else
		memcpy(buf + 2 + len + 1, "netascii", 8);
	memcpy(buf + 2 + len + 1 + type, "\0", 1);
	datalen = len + 2 + 1 + type + 1;  //datalen�����ݳ��ȴ��ݳ�ȥ
	return buf;
}

//����WRQ���ݰ�
char* RequestUploadPack(char* content, int& datalen, int type)
{
	int len = strlen(content);
	char* buf = new char[len + 2 + 2 + type];  //����һ����С���ú��ʵĿռ���WRQ
	buf[0] = 0x00;
	buf[1] = 0x02;			 //WRQ�ı����0x02
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

//����ACK���ݰ�
char* AckPack(short& no)
{
	char* ack = new char[4];
	ack[0] = 0x00;
	ack[1] = 0x04;				//ACK���ݰ��ı����0x04
	no = htons(no);				//���������ֽ���ת��Ϊ�����ֽ���
	memcpy(ack + 2, &no, 2);
	no = ntohs(no);				//�������ֽ���ת��Ϊ�����ֽ���
	return ack;
}

//����DATA���ݰ�
char* MakeData(short& no, FILE* f, int& datalen)
{
	char temp[512];
	int sum = fread(temp, 1, 512, f);//����512���ֽڵ��ļ�����
	if (!ferror(f))
	{
		char* buf = new char[4 + sum];
		buf[0] = 0x00;
		buf[1] = 0x03;//DATA���ݰ��ı����0x03
		no = htons(no);
		memcpy(buf + 2, &no, 2);
		no = ntohs(no);
		memcpy(buf + 4, temp, sum);
		datalen = sum + 4;//ͨ�����ñ���datalen���������ݰ�����
		return buf;
	}
	else
		return NULL;
}

//����ERROR���ݰ�
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

//���ա���-��-��-ʱ-��-�롱�ĸ�ʽ�����ǰʱ�䣬���ڹ�����־�ļ�
void print_time(FILE* fp)
{
	time_t t;
	time(&t);		//��ȡ��ǰʱ��
	char stime[100];
	strcpy(stime, ctime(&t));		//ctime��time_t����ת��Ϊ�����׶���ʱ���ʽ
	*(strchr(stime, '\n')) = '\0';		//ת�����������һ���س��������ûس���ȥ��
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
	char commonbuf[2048];	//������
	int buflen = 0;			//���泤��
	int Numbertokill;		//numbertokill��ʾһ�����ݰ�recvfrom��ʱ�Ĵ���
	int Killtime;			//killtime��ʾsendto��ʱ�Ĵ�������recvfrom��ʱ�ֿ�����
	int res, sendlen;
	clock_t start, end;		//����Ŀ�ʼʱ��ͽ���ʱ�䣬���ڼ��㴫������
	double runtime;
	SOCKET sock = getUdpSocket();
	int recvTimeout = 1000;	//����1000ms����1s
	int sendTimeout = 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendTimeout, sizeof(int));

	//���������ϴ��ļ�
	if (op == 1) {
		printf("RRQ����\n");
		print_time(fp);
		fprintf(fp, "receive RRQ for file: %s , datatype: %s\n", name, type);

		FILE* f = fopen(name, "rb");
		//��ʧ��
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
		//��ʼ����
		start = clock();
		char* sendData;
		int datalen = 0;
		short block = 0;
		int RST = 0;		//��¼�ش�����
		int Fullsize = 0;	//��¼�ļ����ܴ�С
		short no = 0;
		res = 1;
		Numbertokill = 1;
		int resend = 1;
		char buf[1024];
		char* ack = AckPack(no);
		memcpy(buf, ack, 4);
		
		while (1) {
			//���û���յ�����
			if (res == -1) {
				if (Numbertokill > 10) {
					printf("No acks get.transmission failed\n");
					print_time(fp);
					fprintf(fp, "Upload file: %s failed.\n", name);//��ӡ������ʾ����������
					break;
				}
				//�ش���һ�����ݰ�
				sendlen = sendto(sock, commonbuf, buflen, 0, (sockaddr*)&client, sizeof(client));
				RST++;
				std::cout << "resend last blk" << std::endl;
				Killtime = 1;		//ͬ�ϴ���sendto��ʱ�����
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
			//�յ�����
			if (res > 0) {
				short flag;
				memcpy(&flag, buf, 2);
				flag = ntohs(flag);
				//ACK
				if (flag == 4) {
					memcpy(&no, buf + 2, 2);
					no = ntohs(no);
					if (no == block) {
						if (feof(f) && datalen != 516) { //����ϴ��ļ��Ѿ�ȫ���ϴ����
							std::cout << "upload finished!" << std::endl; //��������
							end = clock();
							runtime = (double)(end - start) / CLOCKS_PER_SEC; //���㴫��ʱ��
							print_time(fp);
							printf("Average transmission rate: %.2lf kb/s\n", Fullsize / runtime / 1000);
							fprintf(fp, "Upload file: %s finished. resent times: %d. Fullsize: %d\n", name, RST, Fullsize);
							break;
						}

						block++;		//����������һ��DATA��
						sendData = MakeData(block, f, datalen);
						buflen = datalen;
						memcpy(commonbuf, sendData, datalen);		//���»�����
						Fullsize += datalen - 4;
						Numbertokill = 1;			//���õ�ǰ���ݰ����ط�����
						if (sendData == NULL) {		//�������ݰ�ʧ��
							std::cout << "File reading mistakes!" << std::endl;
							break;
						}
						sendlen = sendto(sock, sendData, datalen, 0, (sockaddr*)&client, sizeof(client));		//���ոչ����DATA�����ͳ�ȥ
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
						std::cout << "Pack No=" << block << std::endl;		//���û������ǰ���ڴ�������ݰ��ı��
					}
				}
				//ERROR
				if (flag == 5) {
					short errorcode;
					memcpy(&errorcode, buf + 2, 2);		//���ERROR������ô�����
					errorcode = ntohs(errorcode);
					char strError[1024];		//������Ⲣ��ô�����ϸ��Ϣ
					int iter = 0;
					while (*(buf + iter + 4) += 0) {
						memcpy(strError + iter, buf + iter + 4, 1);
						++iter;
					}
					*(strError + iter + 1) = '\0';
					std::cout << "Error" << errorcode << " " << strError << std::endl;		//���������Ϣ
					print_time(fp);
					fprintf(fp, "Error %d %s\n", errorcode, strError);		//����־�ļ����������Ϣ
					break;		//��������
				}
			}
			res = recvfrom(sock, buf, 1024, 0, (sockaddr*)&client, &len);
		}

	}
	//�ϴ����󣬽����ļ�
	else if (op == 2) {
		printf("WRQ�ϴ�\n");
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
		while (sendlen != 4) {		//���ͳ��Ȳ�Ϊ4˵��ACK��sendto�����˴���
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

		//��ʼ����
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
					ack = AckPack(no);		//�Ը����ݰ�����ACK(������ע�Ƿ���һ�������ݰ�)
					sendlen = sendto(sock, ack, 4, 0, (sockaddr*)&client, sizeof(client));
					Killtime = 1;
					while (sendlen != 4) {		//���ͳ��Ȳ�Ϊ4˵��ACK��sendto�����˴���
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

					if (no == want_recv) {		//want recv����ά���û���ǰ�����յ�����һ�����ݰ��ı��
						Numbertokill = 1;
						memcpy(commonbuf, ack, 4);//����commonbuf�����ݣ�����ʱ����ʱ���ͻ��˽��᲻���ش��Ե�ǰ�յ��ı������һ��(��������յ���һ��)DATA��ACK
						fwrite(buf + 4, res - 4, 1, f);
						Fullsize += res - 4;
						if (res - 4 >= 0 && res - 4 < 512) {		//�����ǰ���ݰ��Ĵ�СС��512bytes��˵���������
							std::cout << "download finished!" << std::endl;
							finish = 1;
							recvTimeout = 5000;
							setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(int));

							end = clock();
							runtime = (double)(end - start) / CLOCKS_PER_SEC;		//����ʱ��
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
					memcpy(&errorcode, buf + 2, 2);		//���ERROR������ô�����
					errorcode = ntohs(errorcode);
					char strError[1024];		//������Ⲣ��ô�����ϸ��Ϣ
					int iter = 0;
					while (*(buf + iter + 4) += 0) {
						memcpy(strError + iter, buf + iter + 4, 1);
						++iter;
					}
					*(strError + iter + 1) = '\0';
					std::cout << "Error" << errorcode << " " << strError << std::endl;		//���������Ϣ
					print_time(fp);
					fprintf(fp, "Error %d %s\n", errorcode, strError);		//����־�ļ����������Ϣ
					break;		//��������
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

	/***********************���ı���IP***********************/
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

	//���߳�
	while (1) {
		int res = recvfrom(listen_sock, request, 1024, 0, (sockaddr*)&client, &len);
		if (res > 0)
			handleClient(client, len, request);
	}
	closesocket(listen_sock);
	WSACleanup();
	return 0;
}