#include <util/format.h>
#include <util/opts.h>
#include <util/unit.h>

#include <math/geometry.h>
#include <math/xform.h>
#include <math/quaternion.h>
#include <math/transform.h>
#include <math/util.h>

#include <win32/win32.h>
#include <win32/panic.h>
#include <win32/cpuid.h>
#include <win32/cpuinfo.h>
#include <win32/window.h>
#include <win32/time.h>
#include <win32/file.h>
#include <win32/stdstream.h>
#include <win32/mman.h>
#include <win32/thread.h>
#include <win32/mutex.h>
#include <win32/event.h>
#include <win32/conditionvar.h>

#include <sched/scheduler.h>
#include <sched/pool.h>
#include <sched/job.h>

#include <uniforms.h>
#include <gx/gx.h>
#include <gx/info.h>
#include <gx/buffer.h>
#include <gx/vertex.h>
#include <gx/program.h>
#include <gx/pipeline.h>
#include <gx/texture.h>
#include <gx/framebuffer.h>
#include <gx/renderpass.h>
#include <gx/resourcepool.h>
#include <gx/memorypool.h>
#include <gx/commandbuffer.h>

#include <ft/font.h>

#include <ui/ui.h>
#include <ui/cursor.h>
#include <ui/frame.h>
#include <ui/layout.h>
#include <ui/window.h>
#include <ui/button.h>
#include <ui/slider.h>
#include <ui/label.h>
#include <ui/dropdown.h>
#include <ui/textbox.h>
#include <ui/console.h>

#include <py/python.h>
#include <py/object.h>
#include <py/exception.h>
#include <py/types.h>
#include <py/collections.h>
#include <py/modules/btmodule.h>

#include <yaml/document.h>
#include <yaml/node.h>

#include <bt/bullet.h>
#include <bt/world.h>
#include <bt/rigidbody.h>
#include <bt/ray.h>

#include <resources.h>
#include <res/res.h>
#include <res/manager.h>
#include <res/resource.h>
#include <res/handle.h>
#include <res/text.h>
#include <res/shader.h>
#include <res/image.h>

#include <mesh/util.h>
#include <mesh/halfedge.h>
#include <mesh/obj.h>

#include <hm/hamil.h>
#include <hm/entity.h>
#include <hm/entityman.h>
#include <hm/component.h>
#include <hm/componentman.h>
#include <hm/components/all.h>

#include <cli/cli.h>

#include <vector>
#include <array>
#include <utility>
#include <atomic>

//int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
int main(int argc, char *argv[])
{
  if(argc > 1) {
    if(auto exit_code = cli::args(argc, argv)) exit(exit_code);
  }

  win32::init();

  constexpr vec2 WindowSize       = { 1600, 900 };
  constexpr ivec2 FramebufferSize = { 1280, 720 };

  using win32::Window;
  Window window(WindowSize.x, WindowSize.y);

  gx::init();
  ft::init();
  ui::init();
  py::init();
  bt::init();
  res::init();
  hm::init();

  printf("numPhysicalProcessors(): %2u\n", win32::cpuinfo().numPhysicalProcessors());
  printf("numLogicalProcessors():  %2u\n", win32::cpuinfo().numLogicalProcessors());

  gx::ResourcePool pool(64);
  gx::MemoryPool memory(1024);

  sched::WorkerPool worker_pool;
  worker_pool
    .acquireWorkerGlContexts(window)
    .kickWorkers();

  auto bunny_fmt = gx::VertexFormat()
    .attr(gx::f32, 3)
    .attr(gx::f32, 3)
    .attrAlias(0, gx::f32, 2);

  gx::VertexBuffer bunny_vbuf(gx::Buffer::Static);
  gx::IndexBuffer bunny_ibuf(gx::Buffer::Static, gx::u16);
  bunny_vbuf.label("bvBunny");
  bunny_ibuf.label("biBunny");

  auto bunny_arr_id = pool.create<gx::IndexedVertexArray>("iaBunny",
    bunny_fmt, bunny_vbuf, bunny_ibuf);
  auto& bunny_arr = pool.get<gx::IndexedVertexArray>(bunny_arr_id);

  auto bunny_load_job = sched::create_job([&]() -> size_t {
    win32::File bunny_f("bunny0.obj", win32::File::Read, win32::File::OpenExisting);
    auto bunny = bunny_f.map(win32::File::ProtectRead);

    auto obj_loader = mesh::ObjLoader().load(bunny.get<const char>(), bunny_f.size());
    const auto& bunny_mesh = obj_loader.mesh();

    bunny.unmap();

    printf("bunny vertices: %zu\n"
      "bunny faces: %zu\n",
      bunny_mesh.vertices().size(), bunny_mesh.faces().size());

    bunny_vbuf.init(sizeof(vec3)*2, bunny_mesh.vertices().size());
    bunny_ibuf.init(sizeof(u16)*3, bunny_mesh.faces().size());

    auto bunny_vbuf_view = bunny_vbuf.map(gx::Buffer::Write, gx::Buffer::MapInvalidate);
    auto bunny_ibuf_view = bunny_ibuf.map(gx::Buffer::Write, gx::Buffer::MapInvalidate);

    auto bunny_verts = bunny_vbuf_view.get<vec3>();
    const auto& bunny_v = bunny_mesh.vertices();
    const auto& bunny_vn = bunny_mesh.normals();
    for(size_t i = 0; i < bunny_v.size(); i++) {
      *bunny_verts++ = bunny_v[i];
      *bunny_verts++ = bunny_vn[i];
    }

    auto bunny_inds = bunny_ibuf_view.get<u16>();
    auto bunny_inds_count = bunny_mesh.faces().size() * 3;
    for(const auto& face : bunny_mesh.faces()) {
      for(const auto& v : face) *bunny_inds++ = (u16)v.v;
    }

    bunny_vbuf_view.unmap();
    bunny_ibuf_view.unmap();

    printf("bunny loaded! (%zu indices)\n", bunny_inds_count);

    return bunny_inds_count;
  });
  auto bunny_load_job_id = worker_pool.scheduleJob(bunny_load_job.withParams());

  auto world = bt::DynamicsWorld();

  py::set_global("world", py::DynamicsWorld_FromDynamicsWorld(world));

  res::load(R.shader.shaders.ids);
  res::load(R.image.ids);

  ft::Font face(ft::FontFamily("georgia"), 35);
  ft::Font small_face(ft::FontFamily("segoeui"), 12);
  auto monospace_face_ptr = std::make_shared<ft::Font>(ft::FontFamily("consola"), 12, &pool);

  ui::CursorDriver cursor(1280/2, 720/2);
  vec3 pos{ 0, 0, 0 };
  float pitch = 0, yaw = 0;
  float zoom = 1.0f, rot = 0.0f;

  vec3 sun { -120.0f, 160.0f, 140.0f };

  int animate = -1;

  auto fmt = gx::VertexFormat()
    .attr(gx::f32, 3)
    .attr(gx::f32, 3)
    .attrAlias(0, gx::f32, 2);

  res::Handle<res::Image> r_texture = R.image.tex;

  auto tex_id = pool.createTexture<gx::Texture2D>("t2dFloor", gx::rgb);
  auto& tex = pool.getTexture<gx::Texture2D>(tex_id);
  auto floor_sampler_id = pool.create<gx::Sampler>("sFloor", 
    gx::Sampler::repeat2d_mipmap()
      .param(gx::Sampler::Anisotropy, 16.0f));

  tex.init(r_texture->data(), 0, r_texture->width(), r_texture->height(), gx::rgba, gx::u8);
  tex.generateMipmaps();

  byte cubemap_colors[][3] = {
    { 0x20, 0x20, 0x20, },
    { 0x20, 0x20, 0x20, },
    { 0x20, 0x20, 0x20, },
    { 0x00, 0x00, 0x00, },
    { 0x20, 0x20, 0x20, },
    { 0x20, 0x20, 0x20, },
  };

  auto cubemap_id = pool.createTexture<gx::TextureCubeMap>("tcSkybox", gx::rgb);
  auto& cubemap = pool.getTexture<gx::TextureCubeMap>(cubemap_id);

  for(size_t i = 0; i < gx::Faces.size(); i++) {
    auto face  = gx::Faces[i];
    void *data = cubemap_colors[i];

    cubemap.init(data, 0, face, 1, gx::rgb, gx::u8);
  }

  auto cubemap_sampler_id = pool.create<gx::Sampler>("sSkybox",
    gx::Sampler::repeat2d_linear());

  auto skybox_fmt = gx::VertexFormat()
    .attr(gx::f32, 3);

  auto skybox_mesh = mesh::box(1, 1, 1);

  auto& skybox_verts = std::get<0>(skybox_mesh);
  auto& skybox_inds  = std::get<1>(skybox_mesh);

  gx::VertexBuffer skybox_vbuf(gx::Buffer::Static);
  gx::IndexBuffer  skybox_ibuf(gx::Buffer::Static, gx::u16);
  skybox_vbuf.init(skybox_verts.data(), skybox_verts.size());
  skybox_ibuf.init(skybox_inds.data(), skybox_inds.size());

  auto skybox_arr_id = pool.create<gx::IndexedVertexArray>("iaSkybox",
    skybox_fmt, skybox_vbuf, skybox_ibuf);
  auto& skybox_arr = pool.get<gx::IndexedVertexArray>(skybox_arr_id);

  std::vector<vec2> fullscreen_quad = {
    { -1.0f,  1.0f },
    { -1.0f, -1.0f },
    {  1.0f, -1.0f },
    {  1.0f,  1.0f },
  };

  auto fullscreen_quad_fmt = gx::VertexFormat()
    .attr(gx::f32, 2);
  
  gx::VertexBuffer fullscreen_quad_vbuf(gx::Buffer::Static);
  fullscreen_quad_vbuf.init(fullscreen_quad.data(), fullscreen_quad.size());

  auto fullscreen_quad_arr_id = pool.create<gx::VertexArray>("aFullscreenQuad",
    fullscreen_quad_fmt, fullscreen_quad_vbuf);
  auto& fullscreen_quad_arr = pool.get<gx::VertexArray>(fullscreen_quad_arr_id);

  res::Handle<res::Shader> r_phong = R.shader.shaders.phong,
    r_program = R.shader.shaders.program,
    r_skybox = R.shader.shaders.skybox,
    r_composite = R.shader.shaders.composite;

  auto program_id = pool.create<gx::Program>("pProgram", gx::make_program(
    r_program->source(res::Shader::Vertex), r_program->source(res::Shader::Fragment), U.program));
  auto& program = pool.get<gx::Program>(program_id);

  auto skybox_program_id = pool.create<gx::Program>("pSkybox", gx::make_program(
    r_skybox->source(res::Shader::Vertex), r_skybox->source(res::Shader::Fragment), U.skybox));
  auto& skybox_program = pool.get<gx::Program>(skybox_program_id);

  auto composite_program_id = pool.create<gx::Program>("pComposite", gx::make_program(
    r_composite->source(res::Shader::Vertex), r_composite->source(res::Shader::Fragment), U.composite));
  auto& composite_program = pool.get<gx::Program>(composite_program_id);

  auto fb_tex_id = pool.createTexture<gx::Texture2D>("t2dFramebufferColor",
    gx::rgb10, gx::Texture::Multisample);
  auto& fb_tex = pool.getTexture<gx::Texture2D>(fb_tex_id);

  auto fb_pos_id = pool.createTexture<gx::Texture2D>("t2dFramebufferPos",
    gx::rgb16f, gx::Texture::Multisample);
  auto& fb_pos = pool.getTexture<gx::Texture2D>(fb_pos_id);

  auto fb_normal_id = pool.createTexture<gx::Texture2D>("t2dFramebufferNormal",
    gx::rgb16f, gx::Texture::Multisample);
  auto& fb_normal = pool.getTexture<gx::Texture2D>(fb_normal_id);

  auto fb_id = pool.create<gx::Framebuffer>("fbFramebuffer");
  auto& fb = pool.get<gx::Framebuffer>(fb_id);

  fb_tex.initMultisample(1, FramebufferSize);
  fb_pos.initMultisample(1, FramebufferSize);
  fb_normal.initMultisample(1, FramebufferSize);

  fb.use()
    .tex(fb_tex, 0, gx::Framebuffer::Color(0))
    .tex(fb_pos, 0, gx::Framebuffer::Color(1))
    .tex(fb_normal, 0, gx::Framebuffer::Color(2))
    .renderbuffer(gx::depth16, gx::Framebuffer::Depth);
  if(fb.status() != gx::Framebuffer::Complete) {
    win32::panic("couldn't create main Framebuffer!", win32::FramebufferError);
  }
  
  auto fb_composite_id = pool.create<gx::Framebuffer>("fbComposite");
  auto& fb_composite = pool.get<gx::Framebuffer>(fb_composite_id);

  fb_composite.use()
    .renderbuffer(FramebufferSize, gx::rgb8, gx::Framebuffer::Color(0));
  if(fb_composite.status() != gx::Framebuffer::Complete) {
    win32::panic("couldn't create composite Framebuffer!", win32::FramebufferError);
  }

  auto resolve_sampler_id = pool.create<gx::Sampler>(gx::Sampler::edgeclamp2d());

  struct SkyboxUniforms {
    mat4 view;
    mat4 persp;
  };

  auto skybox_uniforms_handle = memory.alloc(sizeof(SkyboxUniforms));
  auto& skybox_uniforms = *memory.ptr<SkyboxUniforms>(skybox_uniforms_handle);

  enum : uint {
    FloorTexImageUnit  = 0u,
    SkyboxTexImageUnit = 1u,

    UiTexImageUnit    = 4u,
    SceneTexImageUnit = 5u,
  };

  static constexpr uint MatrixBinding = 0;
  struct MatrixBlock {
    mat4 modelview;
    mat4 projection;
    mat4 normal;
    mat4 tex;
  };

  enum Material : uint {
    Unshaded,
    PhongColored,
    PhongProceduralColored,
    PhongTextured,
  };

  static constexpr uint MaterialBinding = 1;
  struct MaterialBlock {
    union {
      uint material;
      vec4 pad_;
    };
    vec4 color;

    MaterialBlock() :
      material(0)
    { }
  };

  struct Light {
    vec4 position, color;
  };

  static constexpr uint LightBinding = 2;
  struct LightBlock {
    Light lights[4];
    int num_lights;
  };

  struct BlockGroup {
    gx::MemoryPool::Handle handle;

    MatrixBlock *matrix;
    MaterialBlock *material;
  };

  const uint ubo_block_alignment = pow2_round((uint)gx::info().minUniformBindAlignment());
  auto ubo_align = [&](uint sz) {
    return pow2_align(sz, ubo_block_alignment);
  };

  uint material_block_offset = ubo_align(sizeof(MatrixBlock));
  uint block_group_size = material_block_offset + ubo_align(sizeof(MaterialBlock));

  auto alloc_block_group = [&]() -> BlockGroup {
    BlockGroup b;

    b.handle = memory.alloc(block_group_size);

    b.matrix   = memory.ptr<MatrixBlock>(b.handle);
    b.material = memory.ptr<MaterialBlock>(b.handle + material_block_offset);

    return b;
  };

  auto block_group = alloc_block_group();

  auto scene_ubo_id = pool.createBuffer<gx::UniformBuffer>("buScene", gx::Buffer::Stream);
  pool.getBuffer<gx::UniformBuffer>(scene_ubo_id).init(block_group_size, 1);

  auto light_block_handle = memory.alloc(sizeof(LightBlock));
  auto& light_block = *memory.ptr<LightBlock>(light_block_handle);

  auto light_ubo_id = pool.createBuffer<gx::UniformBuffer>("buLight", gx::Buffer::Stream);
  pool.getBuffer<gx::UniformBuffer>(light_ubo_id).init(sizeof(LightBlock), 1);

  program.uniformBlockBinding("MatrixBlock", MatrixBinding);
  program.uniformBlockBinding("MaterialBlock", MaterialBinding);
  program.uniformBlockBinding("LightBlock", LightBinding);

  skybox_program.use()
    .uniformSampler(U.skybox.uEnvironmentMap, 1);

  composite_program.use()
    .uniformSampler(U.composite.uUi, 4)
    .uniformSampler(U.composite.uScene, 5);

  auto scene_pass_id = pool.create<gx::RenderPass>();
  auto& scene_pass = pool.get<gx::RenderPass>(scene_pass_id)
    .framebuffer(fb_id)
    .textures({
      { FloorTexImageUnit,  { tex_id, floor_sampler_id }},
      { SkyboxTexImageUnit, { cubemap_id, cubemap_sampler_id }}
    })
   .uniformBuffersRange({
      { MatrixBinding,   { scene_ubo_id, 0, ubo_align(sizeof(MatrixBlock)) } },
      { MaterialBinding, { scene_ubo_id, material_block_offset, ubo_align(sizeof(MaterialBlock)) } },
    })
    .uniformBuffer(LightBinding, light_ubo_id)
    .pipeline(gx::Pipeline()
      .viewport(0, 0, FramebufferSize.x, FramebufferSize.y)
      .depthTest(gx::Pipeline::LessEqual)
      .cull(gx::Pipeline::Back)
      .seamlessCubemap()
      .noBlend()
      .clear(vec4{ 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f))
    .subpass(gx::RenderPass::Subpass()
      .pipeline(gx::Pipeline()
        .depthTest(gx::Pipeline::LessEqual)
        .cull(gx::Pipeline::Front)))
    .clearOp(gx::RenderPass::ClearColorDepth)
    ;

  auto composite_pass_id = pool.create<gx::RenderPass>();
  auto& composite_pass = pool.get<gx::RenderPass>(composite_pass_id)
    .framebuffer(fb_composite_id)
    .textures({
      { SceneTexImageUnit, { fb_tex_id, resolve_sampler_id }}
    })
    .pipeline(gx::Pipeline()
      .viewport(0, 0, FramebufferSize.x, FramebufferSize.y)
      .noDepthTest()
      .clear(vec4{ 0.0f, 0.0f, 0.0f, 0.0f, }, 1.0f))
    .clearOp(gx::RenderPass::ClearColor)
    ;

  float r = 1280.0f;
  float b = 720.0f;

  mat4 ortho = xform::ortho(0, 0, b, r, 0.1f, 1000.0f);

  mat4 zoom_mtx = xform::identity();

  window.captureMouse();

  float old_fps;

  bool ortho_projection = false;

  auto sphere = mesh::sphere(16, 16);

  auto& sphere_verts = std::get<0>(sphere);
  auto& sphere_inds  = std::get<1>(sphere);

  vec3 light_position[] = {
    { 0, 6, 0 },
    { -10, 6, -10 },
    { 20, 6, 0 },
  };

  struct FloorVtx {
    vec3 pos;
    vec3 normal;
    vec2 tex_coord;
  };

  std::vector<FloorVtx> floor_vtxs = {
    { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f } },
    { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 0.0f, 0.0f } },
    { {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 1.0f, 0.0f } },

    { {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 1.0f, 0.0f } },
    { {  1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 1.0f, 1.0f } },
    { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f } },

    { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },
    { {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f } },
    { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f } },

    { {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f } },
    { { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },
    { {  1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } },
  };

  auto floor_fmt = gx::VertexFormat()
    .attr(gx::f32, 3)
    .attr(gx::f32, 3)
    .attr(gx::f32, 2);

  gx::VertexBuffer floor_vbuf(gx::Buffer::Static);
  floor_vbuf.init(floor_vtxs.data(), floor_vtxs.size());

  gx::VertexArray floor_arr(floor_fmt, floor_vbuf);

  gx::VertexBuffer sphere_vbuf(gx::Buffer::Static);
  gx::IndexBuffer  sphere_ibuf(gx::Buffer::Static, gx::u16);

  sphere_vbuf.init(sphere_verts.data(), sphere_verts.size());
  sphere_ibuf.init(sphere_inds.data(), sphere_inds.size());

  gx::IndexedVertexArray sphere_arr(fmt, sphere_vbuf, sphere_ibuf);

  auto line_fmt = gx::VertexFormat()
    .attr(gx::f32, 3)
    .attrAlias(0, gx::f32, 3)
    .attrAlias(0, gx::f32, 2);

  auto line = mesh::box(0.05f, 0.5f, 0.05f);
  auto line_vtxs = std::get<0>(line);
  auto line_inds = std::get<1>(line);

  gx::VertexBuffer line_vbuf(gx::Buffer::Static);
  gx::IndexBuffer line_ibuf(gx::Buffer::Static, gx::u16);

  line_vbuf.init(line_vtxs.data(), line_vtxs.size());
  line_ibuf.init(line_inds.data(), line_inds.size());

  gx::IndexedVertexArray line_arr(line_fmt, line_vbuf, line_ibuf);
  ui::Ui iface(pool, ui::Geometry{ 0, 0, WindowSize.x, WindowSize.y }, ui::Style::basic_style());

  composite_pass.texture(UiTexImageUnit, iface.framebufferTextureId(), resolve_sampler_id);

  iface.realSize(FramebufferSize.cast<float>());

  auto& layout = ui::create<ui::RowLayoutFrame>(iface)
    .frame<ui::PushButtonFrame>(iface, "b")
    .frame(ui::create<ui::HSliderFrame>(iface, "fov")
            .range(60.0f, 120.0f))
    .frame(ui::create<ui::LabelFrame>(iface, "fov_val")
            .caption(util::fmt("Fov: %.2f  ", 0.0f))
            .padding({ 20.0f, 0.0f })
            .gravity(ui::Frame::Center))
    ;

  auto btn_b = iface.getFrameByName<ui::PushButtonFrame>("b");
  btn_b->caption("Quit Application").onClick([&](auto target) {
    window.quit();
  });

  auto& fov_slider = *iface.getFrameByName<ui::HSliderFrame>("fov");
  auto& fov_val = *iface.getFrameByName<ui::LabelFrame>("fov_val");

  fov_slider.onChange([&](ui::SliderFrame *target) {
    fov_val.caption(util::fmt("Fov: %.2lf", target->value()));
  });
  fov_slider.value(70.0f);

  auto& stats_layout = ui::create<ui::RowLayoutFrame>(iface)
    .frame(ui::create<ui::LabelFrame>(iface, "stats")
            .gravity(ui::Frame::Left))
    ;

  auto& stats = *iface.getFrameByName<ui::LabelFrame>("stats");

  res::Handle<res::Image> r_hahabenis = R.image.hahabenis,
    r_logo = R.image.logo,
    r_benis = R.image.benis;

  auto hahabenis = iface.drawable().fromImage(r_hahabenis->data(),
    r_hahabenis->width(), r_hahabenis->height());
  auto logo = iface.drawable().fromImage(r_logo->data(),
    r_logo->width(), r_logo->height());
  auto benis = iface.drawable().fromImage(r_benis->data(),
    r_benis->width(), r_benis->height());

  auto& hamil_layout = ui::create<ui::RowLayoutFrame>(iface)
    .frame(ui::create<ui::LabelFrame>(iface)
      .drawable(hahabenis)
      .padding({ 256.0f, 256.0f }))
    .frame(ui::create<ui::LabelFrame>(iface)
      .drawable(logo))
    //.frame(ui::create<ui::LabelFrame>(iface)
    //  .drawable(benis))
    ;

  iface
    .frame(ui::create<ui::WindowFrame>(iface)
      .title("Window")
      .content(layout)
      .position({ 30.0f, 500.0f }))
    .frame(ui::create<ui::WindowFrame>(iface)
      .title("Statistics")
      .content(stats_layout)
      .position({ 1000.0f, 100.0f }))
    .frame(ui::create<ui::WindowFrame>(iface)
      .title("Hamil")
      .content(hamil_layout)
      .background(ui::white())
      .position({ 800.0f, 400.0f }))
    .frame(ui::create<ui::ConsoleFrame>(iface, "g_console"))
    ;

  auto& console = *iface.getFrameByName<ui::ConsoleFrame>("g_console");

  console.onCommand([&](auto target, const char *command) {
    try {
      py::exec(command);
      console.print(">>> " + win32::StdStream::gets());
    } catch(py::Exception e) {
      std::string exception_type = e.type().name();
      if(exception_type == "SystemExit") exit(0);

      console.print(exception_type);
      console.print(e.value().str());
    }
  });

  auto fps_timer = win32::DeltaTimer();
  auto anim_timer = win32::LoopTimer().durationSeconds(2.5);
  auto step_timer = win32::DeltaTimer();
  auto nudge_timer = win32::DeltaTimer();

  step_timer.reset();
  nudge_timer.stop();

  auto scene = hm::entities().createGameObject("Scene");

  const int PboSize = sizeof(u8)*3 * FramebufferSize.area();

  auto pbo_id = pool.createBuffer<gx::PixelBuffer>("bpTest",
    gx::Buffer::DynamicRead, gx::PixelBuffer::Download);
  auto& pbo = pool.getBuffer<gx::PixelBuffer>(pbo_id);
  pbo.init(1, PboSize);

  auto floor_shape = bt::shapes().box({ 50.0f, 0.5f, 50.0f });
  auto create_floor = [&]()
  {
    vec3 origin = { 0.0f, -1.5f, -6.0f };
    auto body = bt::RigidBody::create(floor_shape, origin)
      .rollingFriction(0.2f);
#if 0
      .createMotionState(
#endif

    auto floor = hm::entities().createGameObject("floor", scene);

    floor.addComponent<hm::Transform>(
      origin + vec3{ 0.0f, 0.5f, 0.0f },
      quat::from_euler(PIf/2.0f, 0.0f, 0.0f),
      vec3(50.0f)
    );
    floor.addComponent<hm::RigidBody>(body);

    world.addRigidBody(body);

    return floor;
  };

  auto sphere_shape = bt::shapes().sphere(1.0f);
  unsigned num_spheres = 0;
  auto create_sphere = [&]()
  {
    vec3 origin = { 0.0f, 10.0f, 0.0f };
    auto body = bt::RigidBody::create(sphere_shape, origin, 1.0f);

    auto name = util::fmt("sphere%u", num_spheres);
    auto entity = hm::entities().createGameObject(name, scene);

    entity.addComponent<hm::Transform>(body.worldTransform());
    entity.addComponent<hm::RigidBody>(body);

    world.addRigidBody(body);

    num_spheres++;

    return entity;
  };

  auto floor = create_floor();

  auto cmd_upload_ubos = gx::CommandBuffer::begin()
    .bufferUpload(scene_ubo_id, block_group.handle, block_group_size)
    .end();

  auto cmd_skybox = gx::CommandBuffer::begin()
    .subpass(0)
    .program(skybox_program_id)
    .uniformMatrix4x4(U.skybox.uView, skybox_uniforms_handle)
    .uniformMatrix4x4(U.skybox.uProjection, skybox_uniforms_handle+sizeof(mat4))
    .drawIndexed(gx::Triangles, skybox_arr_id, skybox_inds.size())
    .end();

  cmd_upload_ubos.bindResourcePool(&pool);
  cmd_upload_ubos.bindMemoryPool(&memory);

  cmd_skybox.bindResourcePool(&pool);
  cmd_skybox.bindMemoryPool(&memory);

  auto ui_paint_job = sched::create_job([&]() -> gx::CommandBuffer {

    // Proof-of-concept for generating gx::CommandBuffers
    //   concurrently - the idea is that although OpenGL
    //   is single-threaded in nature, we can still upload
    //   all the buffers (which is one of the few things the 
    //   driver doesn't serialize accross threads) and record
    //   all the draw commands on a separate thread (which 
    //   MUST be MORE expensive or AT LEAST as expensive as
    //   the GL calls themselves otherwise the overhead of
    //   marshalling them into the gx::CommandBuffer makes
    //   this slower than doing it all on a single thread)
    //   and delegate their execution to the main thread,
    //   which (in theory) results in increased performace
    return iface.paint();
  });

  auto physics_step_job = sched::create_job([&](float step_dt) -> Unit {
    world.step(step_dt);

    return {};
  });

  auto transforms_extract_job = sched::create_job([&]() -> Unit {
    // Update Transforms
    hm::components().foreach([&](hmRef<hm::RigidBody> rb) {
      if(rb().rb.isStatic()) return;

      auto entity = rb().entity();
      auto transform = entity.component<hm::Transform>();

      transform() = rb().rb.worldTransform();
    });

    return {};
  });

  double transforms_extract_dt = 0.0;

  while(window.processMessages()) {
    using hm::entities;
    using hm::components;

    auto std_stream = win32::StdStream::gets();

    win32::Timers::tick();
    if(std_stream.size()) console.print(std_stream);

    vec4 eye{ 0, 0, 60.0f/zoom, 1 };

    mat4 eye_mtx = xform::Transform()
      .translate(-pos)
      .rotx(-pitch)
      .roty(yaw)
      .translate(pos * 2.0f)
      .matrix()
      ;
    eye = eye_mtx*eye;

    float nudge_force = 0.0f;

    while(auto input = window.getInput()) {
      cursor.input(input);

      if(iface.input(cursor, input)) continue;

      if(auto kb = input->get<win32::Keyboard>()) {
        using win32::Keyboard;

        if(kb->keyDown('S')) {
          components().foreach([&](hmRef<hm::RigidBody> rb) {
            world.removeRigidBody(rb().rb);
          });

          scene.destroy();
          scene = entities().createGameObject("Scene");

          floor = create_floor();
        } else if(kb->keyDown('Q')) {
          window.quit();
        } else if(kb->keyDown('O')) {
          ortho_projection = !ortho_projection;
        } else if(kb->keyDown('D')) {
          create_sphere();
        } else if(kb->keyDown('W')) {
          //pipeline.isEnabled(gx::Pipeline::Wireframe) ? pipeline.filledPolys() : pipeline.wireframe();
        } else if(kb->keyDown('`')) {
          console.toggle();
        } else if(kb->keyDown('F')) {
          pbo.downloadFramebuffer(fb_composite, FramebufferSize.x, FramebufferSize.y,
            gx::rgb, gx::u8);

          //pbo.downloadTexture(tex, 1, gx::rgb, gx::u8);

          auto pbo_view = pbo.map(gx::Buffer::Read, 0, PboSize);

          win32::File screenshot("screenshot.bin", win32::File::Write, win32::File::CreateAlways);
          screenshot.write(pbo_view.get(), (win32::File::Size)PboSize);
        }
      } else if(auto mouse = input->get<win32::Mouse>()) {
        using win32::Mouse;

        cursor.visible(!mouse->buttons);
        if(mouse->buttonDown(Mouse::Left)) iface.keyboard(nullptr);

        if(mouse->buttons & Mouse::Left) {
          mat4 d_mtx = xform::identity()
            *xform::roty(yaw)
            *xform::rotx(-pitch)
            ;
          vec4 d = d_mtx*vec4{ mouse->dx, -mouse->dy, 0, 1 };

          pos -= d.xyz() * (0.01f/zoom);
        } else if(mouse->buttons & Mouse::Right) {
          constexpr float factor = PIf/1024.0f;

          pitch += mouse->dy * factor;
          yaw += mouse->dx * factor;

          pitch = clamp(pitch, (-PIf/3.0f) + 0.01f, (PIf/3.0f) - 0.01f);
        } else if(mouse->event == Mouse::Wheel) {
          zoom = clamp(zoom+(mouse->ev_data/120)*0.05f, 0.01f, INFINITY);
        } else if(mouse->buttonDown(Mouse::Middle)) {
          zoom = 1.0f;

          zoom_mtx = xform::identity();
        }

        if(mouse->buttonDown(Mouse::Left)) {
          nudge_timer.reset();
        } else if(mouse->buttonUp(Mouse::Left)) {
          nudge_force = nudge_timer.elapsedSecondsf();
          nudge_timer.stop();
        }
      }
    }

    // All the input has been processed - schedule a Ui paint
    auto ui_paint_job_id = worker_pool.scheduleJob(ui_paint_job.withParams());

    mat4 model = xform::identity();

    auto persp = xform::perspective(fov_slider.value(),
      (float)FramebufferSize.x/(float)FramebufferSize.y, 50.0f, 10e20f);

    auto view = xform::look_at(eye.xyz(), pos, vec3{ 0, 1, 0 });

    auto texmatrix = xform::Transform()
      .scale(3.0f)
      .matrix()
      ;

    vec4 mouse_ray = xform::unproject({ cursor.pos(), 0.5f }, persp*view, FramebufferSize);
    vec3 mouse_ray_direction = vec4::direction(eye, mouse_ray).xyz();

    bt::RigidBody picked_body;
    hm::Entity picked_entity = hm::Entity::Invalid;
    bool draw_nudge_line = false;
    vec3 hit_normal;
    if(mouse_ray.w != 0.0f) {
      auto hit = world.rayTestClosest(bt::Ray::from_direction(eye.xyz(), mouse_ray_direction));
      if(hit) {
        picked_body = hit.rigidBody();
        picked_entity = hit.rigidBody().user<hm::Entity>();
        hit_normal  = hit.normal();

        if(!picked_body.isStatic()) draw_nudge_line = true;
      }
    }

    if(nudge_force > 0.0f && picked_body) {
      auto center_of_mass = picked_body.centerOfMass();
      float force_factor = 1.0f + pow(nudge_force, 3.0f)*10.0f;

      picked_body
        .activate()
        .applyImpulse(-hit_normal*force_factor, center_of_mass);
    }

    // Kick off the physics simulation - DO NOT touch any physics-related
    //   structures before waiting for completion
    float step_dt = step_timer.elapsedSecondsf();
    auto physics_step_job_id = worker_pool.scheduleJob(
      physics_step_job.withParams(step_dt)
    );

    step_timer.reset();

    vec4 color;

    size_t num_tris = 0;

    light_block.num_lights = 3;

    light_block.lights[0] = {
      view*vec4(light_position[0], 1.0f),
      vec3{ 1.0f, 1.0f, 1.0f }
    };
    light_block.lights[1] = {
      view*vec4(light_position[1], 1.0f),
      vec3{ 1.0f, 1.0f, 0.0f }
    };
    light_block.lights[2] = {
      view*vec4(light_position[2], 1.0f),
      vec3{ 0.0f, 1.0f, 1.0f }
    };

    pool.getBuffer(light_ubo_id).get().upload(&light_block, 0, 1);

    block_group.matrix->projection = persp;
    block_group.matrix->tex = texmatrix;

    auto& scene_ubo = pool.getBuffer(scene_ubo_id);
    auto upload_ubos = [&]() {
#if 0
      auto ubo_view = scene_ubo().map(gx::Buffer::Write, 0, block_group_size, gx::Buffer::MapInvalidate);

      memcpy(ubo_view.get(), memory.ptr(block_group_handle), block_group_size);
#else
      cmd_upload_ubos.execute();
#endif
    };

    auto drawsphere = [&](bool shaded = true)
    {
      mat4 modelview = view*model;

      block_group.matrix->modelview = modelview;
      block_group.matrix->normal = modelview.inverse().transpose();

      block_group.material->material = shaded ? PhongProceduralColored : Unshaded;
      block_group.material->color = color;

      upload_ubos();

      program.use()
        .draw(gx::Triangles, sphere_arr, sphere_inds.size());
      sphere_arr.end();

      num_tris += sphere_inds.size() / 3;
    };

    scene_pass.begin(pool);

    mat4 modelview = view*xform::translate(0.0f, 0.0f, -10.0f)*xform::scale(2.0f);

    block_group.matrix->modelview = modelview;
    block_group.matrix->normal = modelview.inverse().transpose();

    block_group.material->material = PhongColored;
    block_group.material->color = { 0.53f, 0.8f, 0.94f, 1.0f };

    if(bunny_load_job.done()) {
      if(bunny_load_job_id != sched::WorkerPool::InvalidJob) {
        worker_pool.waitJob(bunny_load_job_id);
        bunny_load_job_id = sched::WorkerPool::InvalidJob;
      }

      auto num_inds = bunny_load_job.result();

      auto command_buf = gx::CommandBuffer::begin()
        .bindResourcePool(&pool)
        .bindMemoryPool(&memory)
        .bufferUpload(scene_ubo_id, block_group.handle, block_group_size)
        .program(program_id)
        .drawIndexed(gx::Triangles, bunny_arr_id, num_inds)
        .end();

      command_buf.execute();
      num_tris += num_inds / 3;
    }

    if(draw_nudge_line) {
      float force_factor = 1.0f + pow(nudge_timer.elapsedSecondsf(), 3.0f);

      auto q = quat::rotation_between(vec3::up(), hit_normal);

      model = xform::Transform()
        .translate(0.0f, 0.5f, 0.0f)
        .scale(1.0f, 1.5f*force_factor, 1.0f)
        .rotate(q)
        .translate(picked_body.origin())
        .matrix();

      auto modelview = view*model;

      block_group.matrix->modelview = modelview;
      block_group.matrix->normal = modelview.inverse().transpose();

      block_group.material->material = Unshaded;
      block_group.material->color = { 1.0f, 1.0f/force_factor, 1.0f/force_factor, 1.0f };

      upload_ubos();

      program.use()
        .draw(gx::Triangles, line_arr, line_inds.size());
      line_arr.end();
    }

    std::vector<hm::Entity> dead_entities;

    // Draw the Entities
    components().foreach([&](hmRef<hm::Transform> component) {
      auto entity = component().entity();
      if(!entity.hasComponent<hm::RigidBody>() || entity == floor) return;

      auto origin = component().t.translation();

      if(origin.distance2(vec3::zero()) > 10e3*10e3) {
        dead_entities.push_back(entity);
        return;
      }

      color = { entity == picked_entity ? vec3(1.0f, 0.0f, 0.0f) : vec3(1.0f), 1.0f };
      model = component().t.matrix();

      drawsphere();
    });

    modelview = view * floor.component<hm::Transform>().get().matrix();

    block_group.matrix->modelview = modelview;
    block_group.matrix->normal = modelview.inverse().transpose();

    block_group.material->material = PhongTextured;

    upload_ubos();

    program.use()
      .uniformSampler(U.program.uTex, 0)
      .draw(gx::Triangles, floor_arr, floor_vtxs.size());
    num_tris += floor_vtxs.size() / 3;

    for(int i = 0; i < light_block.num_lights; i++) {
      vec4 pos = light_position[i];
      color = light_block.lights[i].color;
      model = xform::Transform()
        .scale(0.2f)
        .translate(pos)
        .matrix()
        ;

      drawsphere(/* shaded = */ false);
    }

    skybox_uniforms = { view, persp };
    cmd_skybox
      .activeRenderPass(scene_pass_id)
      .execute();

    for(int i = 0; i < light_block.num_lights; i++) {
      model = xform::Transform()
        .scale(0.2f)
        .translate(light_position[i])
        .matrix()
        ;

      vec2 screen = xform::project(vec3{ -1.5f, 1, -1 }, persp*view*model, FramebufferSize);
      screen.y -= 10;

      small_face.draw(util::fmt("Light %d", i+1), screen, { 1.0f, 1.0f, 1.0f });
    }

    if(picked_entity && picked_entity.alive()) {
      if(picked_entity.gameObject().parent() == scene) {
        auto transform = picked_entity.component<hm::Transform>();

        small_face.draw(util::fmt("picked(0x%.8x) at: %s",
          picked_entity.id(), math::to_str(transform.get().t.translation())),
          { 30.0f, 100.0f+small_face.height()*2.8f }, { 1.0f, 0.0f, 0.0f });
      }
    }

    // Draw entity names, ids and origins in columns
    float entity_str_width = 300.0f;
    float entity_str_origin_y = 170.0f;
    float y = entity_str_origin_y;
    float x = 30.0f;
    scene.gameObject().foreachChild([&](hm::Entity entity) {
      if(!entity.hasComponent<hm::RigidBody>()) return;

      if(y > FramebufferSize.y-small_face.height()) {
        x += entity_str_width;
        y = entity_str_origin_y;
      }

      if(x+entity_str_width > FramebufferSize.x) return; // Cull invisible text

      auto transform = entity.component<hm::Transform>();

      small_face.draw(util::fmt("%s(0x%.8x) at: %s",
        entity.gameObject().name(), entity.id(), math::to_str(transform().t.translation())),
        { x, y }, { 1.0f, 1.0f, 1.0f });
      y += small_face.height();
    });

    worker_pool.waitJob(physics_step_job_id);

    auto transforms_extract_job_id = worker_pool.scheduleJob(transforms_extract_job.withParams());

    float fps = 1.0f / step_dt;

    constexpr float smoothing = 0.9f;
    old_fps = fps;
    fps = old_fps*smoothing + fps*(1.0f-smoothing);

    face.draw(util::fmt("FPS: %.2f", fps),
      vec2{ 30.0f, 70.0f }, { 0.8f, 0.0f, 0.0f });

    // Wait for Ui painting to finish
    worker_pool.waitJob(ui_paint_job_id);

    // Display the statistics
    stats.caption(util::fmt(
      "Frametime: %.3lfms\n"
      "Scene triangles: %zu\n"
      "Physics update: %.3lfms\n"
      "Ui painting: %.3lfms\n"
      "Transform extraction: %.3lfms",
      step_dt*1000.0,
      num_tris,
      physics_step_job.dbg_ElapsedTime()*1000.0,
      ui_paint_job.dbg_ElapsedTime()*1000.0,
      transforms_extract_dt*1000.0)
    );

    ui_paint_job.result().execute();
    cursor.paint();

    // Resolve the main Framebuffer and composite the Ui on top of it
    composite_pass.begin(pool);

    composite_program.use()
      .draw(gx::TriangleFan, fullscreen_quad_arr, fullscreen_quad.size());

    fb_composite.blitToWindow(
      ivec4{ 0, 0, FramebufferSize.x, FramebufferSize.y },
      ivec4{ 0, 0, (int)WindowSize.x, (int)WindowSize.y },
      gx::Framebuffer::ColorBit, gx::Sampler::Linear);

    worker_pool.waitJob(transforms_extract_job_id);
    transforms_extract_dt = transforms_extract_job.dbg_ElapsedTime();

    // Kill off dead_entites
    for(auto& e : dead_entities) {
      world.removeRigidBody(e.component<hm::RigidBody>().get().rb);
      e.destroy();
    }

    window.swapBuffers();
    glFinish();
  }

  pool.purge();

  hm::finalize();
  res::finalize();
  bt::finalize();
  py::finalize();
  ui::finalize();
  ft::finalize();
  gx::finalize();

  win32::finalize();

  return 0;
}
