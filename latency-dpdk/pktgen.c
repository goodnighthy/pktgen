#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <sys/time.h>

#define RX_RING_SIZE 256
#define TX_RING_SIZE 512

#define NUM_MBUFS 8192
#define MBUF_SIZE (1600 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define MBUF_CACHE_SIZE 0
#define BURST_SIZE 32
#define NUM_PORTS 2
#define NO_FLAGS 0

static struct rte_mempool *pktmbuf_pool;
static uint8_t keep_running = 1;
static uint16_t len;
static void *header;

static uint64_t timestamp[10000];
static uint32_t _index = 0;
u_char *chars;
uint64_t packet_count = 0;
uint64_t packet_bytes = 0;
#define MAX_LINE 100


struct port_info
{
    uint8_t port;
    uint8_t queue;
};

static const struct rte_eth_conf port_conf_default = {
	.rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN },
	.txmode = {
                .mq_mode = ETH_MQ_TX_NONE,
        },
};

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static int port_init(uint8_t port) {
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = 1;
	const uint16_t tx_rings = 1;

	int retval;
	uint16_t q;

	if (port >= rte_eth_dev_count())
		return -1;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
				rte_eth_dev_socket_id(port), NULL, pktmbuf_pool);
		if (retval < 0)
			return retval;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, TX_RING_SIZE,
				rte_eth_dev_socket_id(port), NULL);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct ether_addr addr;
	rte_eth_macaddr_get(port, &addr);
	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			(unsigned)port,
			addr.addr_bytes[0], addr.addr_bytes[1],
			addr.addr_bytes[2], addr.addr_bytes[3],
			addr.addr_bytes[4], addr.addr_bytes[5]);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	rte_eth_promiscuous_enable(port);
	struct rte_eth_link  link;
        do{
                rte_eth_link_get(port, &link);
        }while(link.link_status == 0);
	rte_delay_ms(1000);
	return 0;
}


static int
init_mbuf_pools(void) {
        const unsigned num_mbufs = NUM_MBUFS * NUM_PORTS;

        /* don't pass single-producer/single-consumer flags to mbuf create as it
         * seems faster to use a cache instead */
        printf("Creating mbuf pool '%s' [%u mbufs] ...\n",
                        "MBUF_POOL", num_mbufs);
        pktmbuf_pool = rte_mempool_create("MBUF_POOL", num_mbufs,
                        MBUF_SIZE, MBUF_CACHE_SIZE,
                        sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init,
                        NULL, rte_pktmbuf_init, NULL, rte_socket_id(), NO_FLAGS);

        return (pktmbuf_pool == NULL); /* 0  on success */
}



static void 
init_header(void) {
    struct ether_hdr *eth;
    struct ipv4_hdr *ipv4;
    struct udp_hdr *udp;
    uint8_t pkt[42] = {
    0x68, 0x05, 0xca, 0x1e, 0xa5, 0xe4,  0x68, 0x05, 0xca, 0x2a, 0x95, 0x62,  0x08, 0x00,  
    0x45, 0x00, 0x00, 0x20,  0x8d, 0x4a,  0x40, 0x00,  0x40, 0x11,  0x8f, 0x7c,  0x0a, 0x00,  
    0x05, 0x04,  0x0a, 0x00, 0x05, 0x03,  0xe4, 0x91,  0x09, 0xc4,  0x00, 0x0c,  0x8f, 0x3d,  //start data here
    //0x00, 0x00,  0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00
    };//udp

    len = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + sizeof(struct udp_hdr);
    printf("header len : %d\n", len);
    header =  rte_malloc("header", len, 0);
    memcpy(header, pkt, len);

    eth = (struct ether_hdr *)header;
    eth->s_addr.addr_bytes[0] = 0x08;
    eth->s_addr.addr_bytes[1] = 0x00;
    eth->s_addr.addr_bytes[2] = 0x27;
    eth->s_addr.addr_bytes[3] = 0xbb;
    eth->s_addr.addr_bytes[4] = 0x13;
    eth->s_addr.addr_bytes[5] = 0xcf;
    eth->d_addr.addr_bytes[0] = 0x08;
    eth->d_addr.addr_bytes[1] = 0x00;
    eth->d_addr.addr_bytes[2] = 0x27;
    eth->d_addr.addr_bytes[3] = 0xe1;
    eth->d_addr.addr_bytes[4] = 0x14;
    eth->d_addr.addr_bytes[5] = 0x83;
    //eth->ether_type = 0x0800; //do not equal to 0800 anyway, i do not know why too

    ipv4 = (struct ipv4_hdr *)((uint8_t *)header + sizeof(struct ether_hdr));
    //ipv4->version_ihl = 0b01000101;
    //ipv4->time_to_live = 255;
    ipv4->next_proto_id = 17;
    ipv4->src_addr = 16885952;
    ipv4->dst_addr = 50463234;
    //ipv4->total_length = 82;
    //ipv4->total_length = 20+8;   //这个字段没啥用


    udp = (struct udp_hdr *)((uint8_t *)header + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));
    udp->src_port = 9;
    udp->dst_port = 9;
    //udp->dgram_len = 160;
};



static void handle_signal(int sig)
{
        if (sig == SIGINT || sig == SIGTERM)
                keep_running = 0;
}

static int lcore_tx_main(void *arg) {
    uint16_t i, j, sent;
    int ret;
    void *data;
    struct port_info *tx = (struct port_info *)arg;
    //struct rte_mbuf *pkt;
    struct rte_mbuf *pkts[BURST_SIZE];
    struct timespec *payload;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Core %d: Running TX thread\n", rte_lcore_id());
    chars = (u_char *)rte_malloc("chars", 2000, 0);
    i = 0;
    for (; i < 2000; ++i) {
        chars[i] = 'a';
    }


    for (; keep_running;) {        
        /*
        pkt = rte_pktmbuf_alloc(pktmbuf_pool);
        pkt->pkt_len = pkt->data_len = 96;// = ipv4->total_length +14
        data = rte_pktmbuf_mtod(pkt, void*);
        rte_memcpy(data, header, len);
        payload = (struct timespec *)((uint8_t *)data + len);
        clock_gettime(CLOCK_REALTIME, payload);
        printf("%lu\n", payload->tv_nsec);
        sent = rte_eth_tx_burst(tx->port, tx->queue, &pkt, 1);
        if (unlikely(sent < 1)) {
            rte_pktmbuf_free(pkt);
        }*/

        //200B --> 40%
        //1400B --> 40%
        //1-12 --> 200
        //13-14 --> 92
        //15-16 --> 400
        //17-18 --> 600
        //19-20 --> 900
        //21-32 --> 1400

        ret = rte_pktmbuf_alloc_bulk(pktmbuf_pool, pkts, BURST_SIZE);
        if (likely(ret == 0)) {
            i = 0;
            //80
            for (; i < 32; ++i) {
                pkts[i]->pkt_len = pkts[i]->data_len = 64+16;// = ipv4->total_length +14  16

                data = rte_pktmbuf_mtod(pkts[i], void*);
                rte_memcpy(data, header, len);
                payload = (struct timespec *)((uint8_t *)data + len);
                clock_gettime(CLOCK_REALTIME, payload);
            }

           /* //200
            for (; i < 12; ++i) {
                pkts[i]->pkt_len = pkts[i]->data_len = 64+16+120;// = ipv4->total_length +14  16

                data = rte_pktmbuf_mtod(pkts[i], void*);
                rte_memcpy(data, header, len);
                payload = (struct timespec *)((uint8_t *)data + len);
                clock_gettime(CLOCK_REALTIME, payload);
                void *pay_data;
                pay_data = (u_char *)((uint8_t *)data + len + 16);

                rte_memcpy(pay_data, chars, 120);
            }
            //92
            for (; i < 14; ++i) {
                pkts[i]->pkt_len = pkts[i]->data_len = 64+16+12;// = ipv4->total_length +14  16

                data = rte_pktmbuf_mtod(pkts[i], void*);
                rte_memcpy(data, header, len);
                payload = (struct timespec *)((uint8_t *)data + len);
                clock_gettime(CLOCK_REALTIME, payload);
                void *pay_data;
                pay_data = (u_char *)((uint8_t *)data + len + 16);

                rte_memcpy(pay_data, chars, 12);
            }
            //400
            for (; i < 16; i++) {
                pkts[i]->pkt_len = pkts[i]->data_len = 64+16+320;// = ipv4->total_length +14  16

                data = rte_pktmbuf_mtod(pkts[i], void*);
                rte_memcpy(data, header, len);
                payload = (struct timespec *)((uint8_t *)data + len);
                clock_gettime(CLOCK_REALTIME, payload);
                void *pay_data;
                pay_data = (u_char *)((uint8_t *)data + len + 16);

                rte_memcpy(pay_data, chars, 320);
            }
            //600
            for (; i < 18; i++) {
                pkts[i]->pkt_len = pkts[i]->data_len = 64+16+520;// = ipv4->total_length +14  16

                data = rte_pktmbuf_mtod(pkts[i], void*);
                rte_memcpy(data, header, len);
                payload = (struct timespec *)((uint8_t *)data + len);
                clock_gettime(CLOCK_REALTIME, payload);
                void *pay_data;
                pay_data = (u_char *)((uint8_t *)data + len + 16);

                rte_memcpy(pay_data, chars, 520);
            }

            //900
            for (; i < 20; i++) {
                pkts[i]->pkt_len = pkts[i]->data_len = 64+16+820;// = ipv4->total_length +14  16

                data = rte_pktmbuf_mtod(pkts[i], void*);
                rte_memcpy(data, header, len);
                payload = (struct timespec *)((uint8_t *)data + len);
                clock_gettime(CLOCK_REALTIME, payload);
                void *pay_data;
                pay_data = (u_char *)((uint8_t *)data + len + 16);

                rte_memcpy(pay_data, chars, 820);
            }
            //1400
            for (; i < 32; i++) {
                pkts[i]->pkt_len = pkts[i]->data_len = 64+16+1320;// = ipv4->total_length +14  16

                data = rte_pktmbuf_mtod(pkts[i], void*);
                rte_memcpy(data, header, len);
                payload = (struct timespec *)((uint8_t *)data + len);
                clock_gettime(CLOCK_REALTIME, payload);
                void *pay_data;
                pay_data = (u_char *)((uint8_t *)data + len + 16);

                rte_memcpy(pay_data, chars, 1320);
            }*/

            sent = rte_eth_tx_burst(tx->port, tx->queue, pkts, BURST_SIZE);
            if (unlikely(sent < BURST_SIZE)) {
                for (j = sent; j < BURST_SIZE; j++)
                    rte_pktmbuf_free(pkts[j]);
            } 
            rte_delay_us(100);
        } else {
            continue;
        }
    }
    return 0;
}

static int lcore_rx_main(void *arg) {
	uint16_t i, j, receive;
    struct port_info *rx = (struct port_info *)arg;
	struct rte_mbuf *pkts[BURST_SIZE];
    struct timespec *payload, now = {0, 0};
    FILE *fp;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

	printf("Core %d: Running RX thread\n", rte_lcore_id());

	for (; keep_running;) {
		
        receive = rte_eth_rx_burst(rx->port, rx->queue, pkts, BURST_SIZE);

            if (likely(receive > 0)) {
                for (i = 0; i < receive; i++) {
                    void *data;
                    data = rte_pktmbuf_mtod(pkts[i], void*);
//                    printf("length: %d\n", len);
                    payload = (struct timespec *)(rte_pktmbuf_mtod(pkts[i], uint8_t *) + len);
                    clock_gettime(CLOCK_REALTIME, &now);
                    //printf("now:%lu,past:%lu,timestamp:%lu\n", now.tv_nsec, payload->tv_nsec, now.tv_nsec - payload->tv_nsec);
                    //printf("now_nsec : %ld , past_nsec : %ld \n", now.tv_nsec, payload->tv_nsec);
                    timestamp[_index] = (1.0e9*now.tv_sec + now.tv_nsec) - (1.0e9*payload->tv_sec + payload->tv_nsec);
                    //timestamp[_index] = now.tv_nsec - payload->tv_nsec;
                    //printf("timestamp:%lu\n", timestamp[_index]);
                    _index++;
                    //u_char *pay_data;
                    //pay_data = (u_char *)((uint8_t *)data + len + 16);
                    //printf("Payload : %s\n", pay_data);
                    _index = _index % 10000;
                    packet_count++;
                    packet_bytes = packet_bytes + pkts[i]->pkt_len;
                    rte_pktmbuf_free(pkts[i]);
                }
            }
        /*if (_index > 100) {
            if (fp=fopen("/home/zzl/test.txt", "w") == NULL) {
                printf("Open Failed\n");
                return -1;
            }*/
//            char ch;
//            ch = fgetc(fp);
//            fprintf("char : %c\n", ch);
//
//            rte_exit(EXIT_FAILURE, "write done\n");

//        }
//        if (_index > 100){

//            for (j = 0; j < _index; j++) {
//                fprintf(fp, "%lu;\n", timestamp[j]);
//            }
//            _index = 0;
//            char str_line[MAX_LINE];
//    while (feof(fp)) {
//        fgets(str_line, MAX_LINE, fp);
//        fprintf("line: %s\n", str_line);
//    }
//            rte_exit(EXIT_FAILURE, "write done\n");
//        }
    }
    return 0;
}


int main(int argc, char *argv[]) {
	int ret;
	uint8_t total_ports, cur_lcore;
    struct port_info *tx = calloc(1, sizeof(struct port_info));
    struct port_info *rx = calloc(1, sizeof(struct port_info));

    tx->port = 0;
    tx->queue = 0;
    rx->port = 1;
    rx->queue = 0;

	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    total_ports = rte_eth_dev_count();
    cur_lcore = rte_lcore_id();
	if (total_ports < 1)
		rte_exit(EXIT_FAILURE, "ports is not enough\n");
    ret = init_mbuf_pools();
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot create needed mbuf pools\n");
    ret = port_init(tx->port);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot init tx port %u\n", tx->port);
    ret = port_init(rx->port);
    if(ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot init rx port %u\n", rx->port);
    init_header();
    cur_lcore = rte_get_next_lcore(cur_lcore, 1, 1);
    if (rte_eal_remote_launch(lcore_tx_main, (void *)tx,  cur_lcore) == -EBUSY) {
        printf("Core %d is already busy, can't use for tx \n", cur_lcore);
        return -1;
    }
    cur_lcore = rte_get_next_lcore(cur_lcore, 1, 1);
    if (rte_eal_remote_launch(lcore_rx_main, (void *)rx,  cur_lcore) == -EBUSY) {
        printf("Core %d is already busy, can't use for rx \n", cur_lcore);
        return -1;
    }


    for (; keep_running;){
        const unsigned sleeptime = 1;

        sleep(sleeptime * 1);

        /* Loop forever: sleep always returns 0 or <= param */
        //while (sleep(sleeptime) <= sleeptime) {
            printf("pps : %lu \n", packet_count);
            packet_count = 0;
            printf("throughput : %lu \n", packet_bytes * 8);
            packet_bytes = 0;
            int current_index = _index;
            int count = 0;
            int j = 0;
            uint64_t total_time = 0;
            for (; j < current_index; ++j) {
                if (timestamp[j] < 100000000) {    //异常的数据
                    total_time += timestamp[j];
                    count++;
                }
            }
            double latency = total_time / count;
            _index = 0;
            printf("Average latency: %lf  \n", latency); 
        //}

    }
	return 0;
}
