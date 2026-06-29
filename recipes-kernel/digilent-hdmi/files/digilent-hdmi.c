// SPDX-License-Identifier: GPL-2.0-only
// Digilent HDMI encoder DRM driver for PYNQ-Z2
// Provides DRM encoder/connector for HDMI output via axi_dynclk clock management

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_atomic_helper.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Digilent Inc.");
MODULE_DESCRIPTION("HDMI DRM encoder driver for PYNQ-Z2");

struct digilent_hdmi {
	struct device *dev;
	struct clk *clk;
	struct i2c_adapter *i2c;
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static inline struct digilent_hdmi *encoder_to_hdmi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct digilent_hdmi, encoder);
}

static void digilent_hdmi_encoder_atomic_enable(struct drm_encoder *encoder,
					       struct drm_atomic_state *state)
{
	struct digilent_hdmi *hdmi = encoder_to_hdmi(encoder);
	if (hdmi->clk)
		clk_enable(hdmi->clk);
}

static void digilent_hdmi_encoder_atomic_disable(struct drm_encoder *encoder,
						struct drm_atomic_state *state)
{
	struct digilent_hdmi *hdmi = encoder_to_hdmi(encoder);
	if (hdmi->clk)
		clk_disable(hdmi->clk);
}

static int digilent_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
					     struct drm_crtc_state *crtc_state,
					     struct drm_connector_state *conn_state)
{
	return 0;
}

static void digilent_hdmi_encoder_atomic_mode_set(struct drm_encoder *encoder,
						 struct drm_crtc_state *crtc_state,
						 struct drm_connector_state *conn_state)
{
	struct digilent_hdmi *hdmi = encoder_to_hdmi(encoder);
	const struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	unsigned long pixel_clk = adjusted_mode->clock * 1000;
	int vrefresh = drm_mode_vrefresh(adjusted_mode);

	if (hdmi->clk)
		clk_set_rate(hdmi->clk, pixel_clk);

	dev_info(hdmi->dev, "HDMI mode: %ux%u@%u (pixel_clk=%lu Hz)\n",
		 adjusted_mode->hdisplay, adjusted_mode->vdisplay,
		 vrefresh, pixel_clk);
}

static const struct drm_encoder_helper_funcs digilent_hdmi_encoder_helpers = {
	.atomic_enable = digilent_hdmi_encoder_atomic_enable,
	.atomic_disable = digilent_hdmi_encoder_atomic_disable,
	.atomic_check = digilent_hdmi_encoder_atomic_check,
	.atomic_mode_set = digilent_hdmi_encoder_atomic_mode_set,
};

static int digilent_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct digilent_hdmi *hdmi = container_of(connector, struct digilent_hdmi, connector);
	struct edid *edid = NULL;
	int ret = 0;

	if (hdmi->i2c) {
		edid = drm_get_edid(connector, hdmi->i2c);
		if (edid) {
			drm_connector_update_edid_property(connector, edid);
			ret = drm_add_edid_modes(connector, edid);
			kfree(edid);
			dev_info(hdmi->dev, "EDID: %d modes\n", ret);
			return ret;
		}
		dev_info(hdmi->dev, "No EDID detected, using fallback modes\n");
	}

	// Fallback: add common modes if no EDID
	drm_add_modes_noedid(connector, 1920, 1080);
	return ret;
}

static enum drm_connector_status digilent_hdmi_connector_detect(
	struct drm_connector *connector, bool force)
{
	struct digilent_hdmi *hdmi = container_of(connector, struct digilent_hdmi, connector);
	dev_dbg(hdmi->dev, "HDMI detect (assuming connected)\n");
	return connector_status_connected;
}

static void digilent_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs digilent_hdmi_connector_funcs = {
	.detect = digilent_hdmi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = digilent_hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs digilent_hdmi_connector_helpers = {
	.get_modes = digilent_hdmi_connector_get_modes,
};

static int digilent_hdmi_probe(struct platform_device *pdev)
{
	printk(KERN_INFO "[HDMI] probe called for %s\n", pdev->name);
	return 0;
}

static void digilent_hdmi_remove(struct platform_device *pdev)
{
	struct digilent_hdmi *hdmi = platform_get_drvdata(pdev);

	if (hdmi->clk)
		clk_disable_unprepare(hdmi->clk);
	if (hdmi->i2c)
		i2c_put_adapter(hdmi->i2c);
}

static const struct of_device_id digilent_hdmi_of_match[] = {
	{ .compatible = "digilent,hdmi" },
	{ }
};
MODULE_DEVICE_TABLE(of, digilent_hdmi_of_match);

static struct platform_driver digilent_hdmi_driver = {
	.probe = digilent_hdmi_probe,
	.remove = digilent_hdmi_remove,
	.driver = {
		.name = "digilent-hdmi",
		.of_match_table = digilent_hdmi_of_match,
	},
};

static int __init digilent_hdmi_init(void)
{
	int ret;
	printk(KERN_INFO "[HDMI] init START\n");
	ret = platform_driver_register(&digilent_hdmi_driver);
	printk(KERN_INFO "[HDMI] init END ret=%d\n", ret);
	return ret;
}

static void __exit digilent_hdmi_exit(void)
{
	printk(KERN_INFO "[HDMI] exit\n");
	platform_driver_unregister(&digilent_hdmi_driver);
}

module_init(digilent_hdmi_init);
module_exit(digilent_hdmi_exit);
