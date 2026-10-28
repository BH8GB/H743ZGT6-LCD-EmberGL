/* EmberGL 3D 动画：旋转圆环，结构照搬官方 examples/08_TightMem。
 * 差异：中间渲染目标用 RGB565（本屏是 16 位并口），不用 8bpp 灰度。
 * 关闭方式：CMakeLists.txt 里把 LCD_ENABLE_EMBERGL 设为 0。 */
#include "lcd_conf.h"

#if LCD_PORT_EMBERGL

#include "lcd_port_embergl.h"
#include "cmsis_os2.h"
#include "egl_mesh.h"
#include "egl_vertex.h"
#include "egl_shaders.h"

EGL_USING_NAMESPACE

typedef graphics_device_lcd gfx_device_t;

/* ---- 几何：官方圆环（p3g_torus.h）---- */
static const EGL_ALIGN(4) uint32_t s_torus_data[] =
{
#include "p3g_torus.h"
};
static p3g_mesh s_torus;
typedef vertex_p48n32 vtx_t;

/* ---- 顶点着色器：位置 + N·L（同 08_TightMem）---- */
struct vs_simple_ndotl
{
  struct vsout { float ndotl; };

  template <class PSIn, class PSOState, class VIn>
  EGL_INLINE void exec(PSIn &psin_, const PSOState &pso_state_,
                       const typename VIn::transform_state &tstate_,
                       const VIn &vin_, uint8_t) const
  {
    psin_.pos = vec4f(get_pos(vin_, tstate_), 1.0f) * pso_state_.obj_to_proj;

    vsout vo;
    vo.ndotl = dot(get_normal(vin_, tstate_), light_obj_dir);
    psin_.set_attribs(vo);
  }

  vec3f light_obj_dir;
};

/* ---- 像素着色器：双面 N·L ---- */
struct ps_simple_ndotl
{
  struct psin
  {
    vec4f pos;
    float ndotl;

    template <class VSOut>
    EGL_INLINE void set_attribs(const VSOut &vo_) { ndotl = vo_.ndotl; }
  };

  template <class PSC>
  EGL_INLINE void exec(rasterizer_pixel_out<PSC> &psout_,
                       const typename PSC::pso_state&,
                       const psin &v0_, const psin &v1_, const psin &v2_,
                       const vec3f &bc_, uint8_t) const
  {
    float ndotl = v0_.ndotl * bc_.x + v1_.ndotl * bc_.y + v2_.ndotl * bc_.z;
    psout_.export_rt0(color * abs(ndotl));
  }

  color_rgbaf color;
};

struct rstate_opaque : rasterizer_psc_base
{
  enum {rt0_fmt      = lcd_embergl_mem_cfg::rt0_fmt};
  enum {depth_format = lcd_embergl_mem_cfg::depth_format};
};

extern "C" int LCD_EGL_DemoInit(void)
{
  if (LCD_EGL_Init() != 0) return 1;

  LCD_EGL_Device().clear_depth(cleardepth_max);

  s_torus.init(s_torus_data);
  if (!s_torus.is_valid()) return 2;

  return 0;
}

extern "C" void LCD_EGL_DemoRun(uint32_t delay_ms)
{
  static float s_rot_x = -1.2f;
  static float s_rot_y = -0.5f;
  static uint16_t s_band = 0U;   /* 当前条带：0 = 上半屏，1 = 下半屏 */
  const uint16_t band_h = (uint16_t)(LCD_EGL_HEIGHT / 2U);

  mat33f obj_rot;
  set_rotation_xyz(obj_rot, s_rot_x, s_rot_y, 0.0f);

  /* 每条带渲染完整一帧的几何，只保留落在该条带内的像素 */
  for (uint16_t b = 0U; b < 2U; ++b)
  {
    uint16_t y0 = (uint16_t)(b * band_h);

    LCD_EGL_BeginFrameBuffered(0x0000U, y0, band_h);

    {
      cameraf cam;
      float aspect = float(gfx_device_t::fb_width) / float(gfx_device_t::fb_height);
      mat44f view2proj = perspective_matrix<float>(60.0f * mathf::deg_to_rad,
                                                   aspect, 5.0f, 20.0f);
      cam.set_view_to_proj(view2proj, 5.0f, 20.0f);
      cam.set_view_to_world(tform3f::identity());

      psc_p3g_mesh<rstate_opaque, vtx_t, vs_static, ps_color<egl::pixfmt_r5g6b5> > pso;
      pso.set_transform(cam, tform3f(obj_rot, vec3f(0.0f, 0.0f, 12.0f)));
      pso.pshader().color.set_rgba8(230, 255, 80, 255);
      pso.set_geometry(s_torus, 0);

      LCD_EGL_Device().dispatch_pso(pso);
    }

    LCD_EGL_Device().commit();
    LCD_EGL_PresentBand();
  }

  s_rot_x += 0.0246f;
  s_rot_y += 0.0642f;
  (void)s_band;

  osDelay(delay_ms);
}

#endif /* LCD_PORT_EMBERGL */
