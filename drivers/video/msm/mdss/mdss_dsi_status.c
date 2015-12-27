/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/iopoll.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/switch.h>

#include "mdss_fb.h"
#include "mdss_dsi.h"
#include "mdss_panel.h"
#include "mdss_mdp.h"

#define STATUS_CHECK_INTERVAL 3000
#include <mach/oppo_project.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <mach/oppo_boot_mode.h>

struct dsi_status_data {
	struct notifier_block fb_notifier;
	struct delayed_work check_status;
	struct msm_fb_data_type *mfd;
	uint32_t check_interval;
};
struct dsi_status_data *pstatus_data;
static uint32_t interval = STATUS_CHECK_INTERVAL;
extern u32 mdss_dsi_panel_cmd_read(struct mdss_dsi_ctrl_pdata *ctrl, char cmd0,
		char cmd1, void (*fxn)(int), char *rbuf, int len);

#define LPTE_GPIO 13
static int te_count=120;
static int te_state = 0;
static struct switch_dev display_switch;
static struct proc_dir_entry *prEntry_dispswitch = NULL;
extern u32 mdss_dsi_panel_cmd_read(struct mdss_dsi_ctrl_pdata *ctrl, char cmd0,
		char cmd1, void (*fxn)(int), char *rbuf, int len);
static int timeout = 0;
extern bool lcd_is_suspended;

int operate_display_switch(void)
{
    int ret = 0;
    pr_info("%s:state=%d.\n", __func__, te_state);

    if(te_state)
        te_state = 0;
    else
        te_state = 1;

    switch_set_state(&display_switch, te_state);
    return ret;
}


static int esd_check(void)
{
   if(timeout < 4)
   {
	  pr_err("not start esd !!!!!!!!\n");
	  timeout ++;
	  te_count = 0;
	  return 0;
	}

	if(te_count < 60)
	{
	    pr_err("LCD recovery tecount:%s :te_count = %d\n",__func__,te_count);
		return 2;
	}
	te_count = 0;
	return 0;
}

static void esd_recover(void)
{
    pr_err(" lcd esd recover !!!!!!!!\n");
    operate_display_switch();
    te_count = 0;
}


static int lcd_eds_test_dispswitch(char *page, char **start, off_t off, int count, int *eof,  void *data)
{
    pr_err("ESD function test--------\n");
    operate_display_switch();
    return 0;
}

/*
 * check_dsi_ctrl_status() - Check DSI controller status periodically.
 * @work  : dsi controller status data
 *
 * This function calls check_status API on DSI controller to send the BTA
 * command. If DSI controller fails to acknowledge the BTA command, it sends
 * the PANEL_ALIVE=0 status to HAL layer.
 */
static void check_dsi_ctrl_status(struct work_struct *work)
{
	struct dsi_status_data *pdsi_status = NULL;
	struct mdss_panel_data *pdata = NULL;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_overlay_private *mdp5_data = NULL;
	struct mdss_mdp_ctl *ctl = NULL;
	int ret = 0;
	int read_ret = 0;
	char buf[2]={0x00,0x00};

	if(lcd_is_suspended == true)
	{
	   return;
	}

	pdsi_status = container_of(to_delayed_work(work),
		struct dsi_status_data, check_status);
	if (!pdsi_status) {
		pr_err("%s: DSI status data not available\n", __func__);
		return;
	}

	pdata = dev_get_platdata(&pdsi_status->mfd->pdev->dev);
	if (!pdata) {
		pr_err("%s: Panel data not available\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
							panel_data);
	if (!ctrl_pdata || !ctrl_pdata->check_status) {
		pr_err("%s: DSI ctrl or status_check callback not available\n",
								__func__);
		return;
	}

	mdp5_data = mfd_to_mdp5_data(pdsi_status->mfd);
	ctl = mfd_to_ctl(pdsi_status->mfd);

	if (pdsi_status->mfd->shutdown_pending) {
		mutex_unlock(&mdp5_data->ov_lock);
	if (ctl->shared_lock)
		mutex_unlock(ctl->shared_lock);
	pr_err("%s: DSI turning off, avoiding BTA status check\n",__func__);
	return;
	}

	/*
	 * For the command mode panels, we return pan display
	 * IOCTL on vsync interrupt. So, after vsync interrupt comes
	 * and when DMA_P is in progress, if the panel stops responding
	 * and if we trigger BTA before DMA_P finishes, then the DSI
	 * FIFO will not be cleared since the DSI data bus control
	 * doesn't come back to the host after BTA. This may cause the
	 * display reset not to be proper. Hence, wait for DMA_P done
	 * for command mode panels before triggering BTA.
	 */

	pr_debug("%s: DSI ctrl wait for ping pong done\n", __func__);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON, false);
	if(lcd_is_suspended != true)
			{

					mdss_dsi_panel_cmd_read(ctrl_pdata,0x0a,0x00,NULL,buf,1);
					mdss_dsi_panel_cmd_read(ctrl_pdata,0x09,0x00,NULL,&buf[1],1);

					if(buf[0]!= 0x1c ||  buf[1]!= 0x80)
					{
					   read_ret = 1;
					   pr_err("shirendong esd  read wrong  buf[0] = %x,buf[1]=%x\n",buf[0],buf[1]);
					}
			}
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF, false);

  if(lcd_is_suspended != true)
   {
	if ((pdsi_status->mfd->panel_power_on)) {
		ret =  esd_check();
			if(ret > 0 || read_ret > 0 )
			   {
				   esd_recover();
			   }
			   else {
			schedule_delayed_work(&pdsi_status->check_status,
				msecs_to_jiffies(pdsi_status->check_interval));
		}
		}
	}
}

/*
 * fb_event_callback() - Call back function for the fb_register_client()
 *			 notifying events
 * @self  : notifier block
 * @event : The event that was triggered
 * @data  : Of type struct fb_event
 *
 * This function listens for FB_BLANK_UNBLANK and FB_BLANK_POWERDOWN events
 * from frame buffer. DSI status check work is either scheduled again after
 * PANEL_STATUS_CHECK_INTERVAL or cancelled based on the event.
 */
static int fb_event_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	struct dsi_status_data *pdata = container_of(self,
				struct dsi_status_data, fb_notifier);
	pdata->mfd = evdata->info->par;

	if (event == FB_EVENT_BLANK && evdata) {
		int *blank = evdata->data;
		switch (*blank) {
		case FB_BLANK_UNBLANK:
			schedule_delayed_work(&pdata->check_status,
				msecs_to_jiffies(pdata->check_interval));
			break;
		case FB_BLANK_POWERDOWN:
			cancel_delayed_work(&pdata->check_status);
			break;
		}
	}
	te_count=120;
	return 0;
}

int __init mdss_dsi_status_init(void)
{
	int rc = 0;
	int boot_mode = 0;

	boot_mode = get_boot_mode();

	if(boot_mode != MSM_BOOT_MODE__NORMAL)
	   return rc;

	pstatus_data = kzalloc(sizeof(struct dsi_status_data), GFP_KERNEL);
	if (!pstatus_data) {
		pr_err("%s: can't allocate memory\n", __func__);
		return -ENOMEM;
	}

	pstatus_data->fb_notifier.notifier_call = fb_event_callback;

	rc = fb_register_client(&pstatus_data->fb_notifier);
	if (rc < 0) {
		pr_err("%s: fb_register_client failed, returned with rc=%d\n",
								__func__, rc);
		kfree(pstatus_data);
		return -EPERM;
	}

    display_switch.name = "dispswitch";
	rc = switch_dev_register(&display_switch);
    if (rc)
    {
        pr_err("Unable to register display switch device\n");
        return rc;
    }

	prEntry_dispswitch = create_proc_entry( "lcd_esd_test", 0666, NULL );
	if(prEntry_dispswitch == NULL){
		rc = -ENOMEM;
		printk(KERN_INFO"init_synaptics_proc: Couldn't create proc entry\n");
	}else{
		prEntry_dispswitch->read_proc = lcd_eds_test_dispswitch;
	}

	pstatus_data->check_interval = interval;
	pr_info("%s: DSI status check interval:%d\n", __func__,	interval);

	INIT_DELAYED_WORK(&pstatus_data->check_status, check_dsi_ctrl_status);

	pr_debug("%s: DSI ctrl status work queue initialized\n", __func__);

	return rc;
}

void __exit mdss_dsi_status_exit(void)
{
	fb_unregister_client(&pstatus_data->fb_notifier);
	cancel_delayed_work_sync(&pstatus_data->check_status);
	kfree(pstatus_data);
	pr_debug("%s: DSI ctrl status work queue removed\n", __func__);
}

module_param(interval, uint, 0);
MODULE_PARM_DESC(interval,
		"Duration in milliseconds to send BTA command for checking"
		"DSI status periodically");

module_init(mdss_dsi_status_init);
module_exit(mdss_dsi_status_exit);

MODULE_LICENSE("GPL v2");
