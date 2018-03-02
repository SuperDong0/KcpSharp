#include "KcpTest.h"
#include "KcpSharp.h"

Starts2000::KcpSharp::KcpTest::KcpTest() : KcpTest(10, 60, 125, 1000)
{
}

Starts2000::KcpSharp::KcpTest::KcpTest(int lostrate, int rttmi, int rttmax, int nmax)
{
	this->lostrate = lostrate;
	this->rttmi = rttmi;
	this->rttmax = rttmax;
	this->nmax = nmax;
}

int Starts2000::KcpSharp::KcpTest::UdpOutput(IntPtr buffer, int len, IntPtr kcp, IntPtr user) {
	auto userToken = GCHandle::FromIntPtr(user).Target;
	vnet->send((int)userToken, buffer.ToPointer(), len);
	return 0;
}

void Starts2000::KcpSharp::KcpTest::Test(int mode)
{
	// ����ģ�����磺������10 % ��Rtt 60ms~125ms
	this->vnet = new LatencySimulator(lostrate, rttmi, rttmax, nmax);

	// ���������˵�� kcp���󣬵�һ������ conv�ǻỰ��ţ�ͬһ���Ự��Ҫ��ͬ
	// ���һ���� user�������������ݱ�ʶ
	auto kcp1 = gcnew Kcp(0x11223344, (Object ^)0);
	auto kcp2 = gcnew Kcp(0x11223344, (Object ^)1);

	auto output = gcnew KcpOutputHandler(this, &KcpTest::UdpOutput);

	// ����kcp���²����������Ϊ udp_output��ģ��udp�����������
	kcp1->SetOutput(output);
	kcp2->SetOutput(output);

	IUINT32 current = iclock();
	IUINT32 slap = current + 20;
	IUINT32 index = 0;
	IUINT32 next = 0;
	IINT64 sumrtt = 0;
	int count = 0;
	int maxrtt = 0;

	// ���ô��ڴ�С��ƽ���ӳ�200ms��ÿ20ms����һ������
	// �����ǵ������ط�����������շ�����Ϊ128
	kcp1->SetWndSize(128, 128);
	kcp2->SetWndSize(128, 128);

	// �жϲ���������ģʽ
	if (mode == 0) {
		// Ĭ��ģʽ
		kcp1->Nodelay(0, 10, 0, 0);
		kcp2->Nodelay(0, 10, 0, 0);
	}
	else if (mode == 1) {
		// ��ͨģʽ���ر����ص�
		kcp1->Nodelay(0, 10, 0, 1);
		kcp2->Nodelay(0, 10, 0, 1);
	}
	else {
		// ��������ģʽ
		// �ڶ������� nodelay-�����Ժ����ɳ�����ٽ�����
		// ���������� intervalΪ�ڲ�����ʱ�ӣ�Ĭ������Ϊ 10ms
		// ���ĸ����� resendΪ�����ش�ָ�꣬����Ϊ2
		// ��������� Ϊ�Ƿ���ó������أ������ֹ
		kcp1->Nodelay(0, 10, 2, 1);
		kcp2->Nodelay(0, 10, 2, 1);

		kcp1->IKcp->rx_minrto = 10;
		kcp1->IKcp->fastresend = 1;
	}

	array<Byte>^ buffer = gcnew array<Byte>(2000);
	pin_ptr<void> pBuffer = &buffer[0];
	int hr;

	IUINT32 ts1 = iclock();

	while (1) {
		isleep(1);
		current = iclock();
		kcp1->Update(iclock());
		kcp2->Update(iclock());

		// ÿ�� 20ms��kcp1��������
		for (; current >= slap; slap += 20) {
			auto pBuf = static_cast<IUINT32 *>(pBuffer);
			pBuf[0] = index++;
			pBuf[1] = current;
			
			kcp1->Send(buffer, 8);
		}

		// �����������磺����Ƿ���udp����p1->p2
		while (1) {
			hr = vnet->recv(1, pBuffer, 2000);
			if (hr < 0) break;
			// ��� p2�յ�udp������Ϊ�²�Э�����뵽kcp2
			kcp2->Input(buffer, hr);
		}

		// �����������磺����Ƿ���udp����p2->p1
		while (1) {
			hr = vnet->recv(0, pBuffer, 2000);
			if (hr < 0) break;
			// ��� p1�յ�udp������Ϊ�²�Э�����뵽kcp1
			kcp1->Input(buffer, hr);
		}

		// kcp2���յ��κΰ������ػ�ȥ
		while (1) {
			hr = kcp2->Recv(buffer, 10);
			// û���յ������˳�
			if (hr < 0) break;
			// ����յ����ͻ���
			kcp2->Send(buffer, hr);
		}

		// kcp1�յ�kcp2�Ļ�������
		while (1) {
			hr = kcp1->Recv(buffer, 10);
			// û���յ������˳�
			if (hr < 0) break;

			auto pBuf = static_cast<char *>(pBuffer);
			IUINT32 sn = *(IUINT32*)(pBuf + 0);
			IUINT32 ts = *(IUINT32*)(pBuf + 4);
			IUINT32 rtt = current - ts;

			if (sn != next) {
				// ����յ��İ�������
				printf("ERROR sn %d<->%d\n", (int)count, (int)next);
				return;
			}

			next++;
			sumrtt += rtt;
			count++;
			if (rtt > (IUINT32)maxrtt) maxrtt = rtt;

			printf("[RECV] mode=%d sn=%d rtt=%d\n", mode, (int)sn, (int)rtt);
		}
		if (next > 1000) break;
	}

	ts1 = iclock() - ts1;

	delete kcp1;
	delete kcp2;

	const char *names[3] = { "default", "normal", "fast" };
	printf("dotnet wrapper KCP test.\n");
	printf("%s mode result (%dms):\n", names[mode], (int)ts1);
	printf("avgrtt=%d maxrtt=%d tx=%d\n", (int)(sumrtt / count), (int)maxrtt, (int)vnet->tx1);
	printf("press enter to next ...\n");
	delete this->vnet;
	char ch; scanf("%c", &ch);
}

Starts2000::KcpSharp::KcpTest::~KcpTest()
{
}

Starts2000::KcpSharp::KcpTest::!KcpTest()
{
	if (this->vnet) {
		delete this->vnet;
	}
}
