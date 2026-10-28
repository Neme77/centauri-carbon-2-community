// SPDX-License-Identifier: GPL-2.0
/* Preliminary ABI/dependency probe. No driver registration or hardware writes. */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/usb.h>

extern const unsigned long cc2_ue300_dependencies[];
static int cc2_probe_usb(struct usb_device *usb, void *data)
{
    unsigned int *seen = data;
    if (le16_to_cpu(usb->descriptor.idVendor) != 0x0bda ||
        le16_to_cpu(usb->descriptor.idProduct) != 0x8153) return 0;
    if (le16_to_cpu(usb->descriptor.bcdDevice) != 0x31fd || usb->speed != USB_SPEED_HIGH)
        return -EINVAL;
    (*seen)++;
    return 0;
}
static int __init cc2_probe_init(void)
{
    struct net_device *netdev;
    struct sk_buff *skb;
    unsigned int i, seen = 0;
    int result = -EINVAL;
    BUILD_BUG_ON(sizeof(struct net_device) != 1600);
    BUILD_BUG_ON(sizeof(struct sk_buff) != 192);
    BUILD_BUG_ON(sizeof(struct module) != 512);
    BUILD_BUG_ON(offsetof(struct net_device, netdev_ops) != 0x120);
    BUILD_BUG_ON(offsetof(struct net_device, priv_flags) != 0x134);
    BUILD_BUG_ON(offsetof(struct net_device, needed_headroom) != 0x152);
    BUILD_BUG_ON(offsetof(struct net_device, dev_addr) != 0x1cc);
    BUILD_BUG_ON(offsetof(struct net_device, watchdog_timeo) != 0x298);
    BUILD_BUG_ON(offsetof(struct sk_buff, head) != 0xb0);
    BUILD_BUG_ON(offsetof(struct sk_buff, data) != 0xb4);
    BUILD_BUG_ON(offsetof(struct sk_buff, users) != 0xbc);
    for (i = 0; i < 112; i++)
        if (!cc2_ue300_dependencies[i]) return -EINVAL;
    netdev = alloc_etherdev(16);
    if (!netdev) return -ENOMEM;
    if (netdev->mtu != 1500 || netdev->hard_header_len != ETH_HLEN ||
        netdev->addr_len != ETH_ALEN || netdev->type != ARPHRD_ETHER ||
        netdev->tx_queue_len != 1000 || netdev->needed_headroom != 0) {
        pr_err("CC2_UE300_ABI_R1 FAIL net_device layout\n");
        goto free_net;
    }
    skb = alloc_skb(256, GFP_KERNEL);
    if (!skb) { result = -ENOMEM; goto free_net; }
    if (skb->len || skb->data_len || refcount_read(&skb->users) != 1 ||
        !skb->head || skb->head != skb->data || skb_tail_pointer(skb) != skb->data) {
        pr_err("CC2_UE300_ABI_R1 FAIL sk_buff layout\n");
        goto free_skb;
    }
    if (usb_for_each_dev(&seen, cc2_probe_usb) || seen != 1) {
        pr_err("CC2_UE300_ABI_R1 FAIL usb_device layout/revision\n");
    } else {
        result = 0;
        pr_info("CC2_UE300_ABI_R1 PASS core dependencies, net_device, sk_buff, USB descriptor; no Ethernet registered\n");
    }

free_skb:
    kfree_skb(skb);
free_net:
    free_netdev(netdev);
    return result;
}
static void __exit cc2_probe_exit(void) { }
module_init(cc2_probe_init);
module_exit(cc2_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CC2 UE300 preliminary ABI probe; no Ethernet driver registration");
