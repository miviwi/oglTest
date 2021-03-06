#include <res/loader.h>
#include <res/manager.h>
#include <res/resource.h>
#include <res/text.h>
#include <res/shader.h>
#include <res/image.h>
#include <res/texture.h>
#include <res/mesh.h>
#include <res/lut.h>

#include <util/format.h>
#include <os/file.h>
#include <os/panic.h>
#include <os/mutex.h>
#include <yaml/document.h>
#include <yaml/node.h>
#include <yaml/schema.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>

namespace res {

ResourceManager& ResourceLoader::manager()
{
  return *m_man;
}

ResourceLoader *ResourceLoader::init(ResourceManager *manager)
{
  m_man = manager;

  doInit();

  return this;
}

Resource::Ptr ResourceLoader::load(Resource::Id id, LoadFlags flags)
{
  assert(m_man && "load() called before init()!");

  return doLoad(id, flags);
}

SimpleFsLoader::SimpleFsLoader(const char *base_path) :
  m_path(base_path),
  m_available_mutex(os::Mutex::alloc())
{
}

static yaml::Schema p_meta_generic_schema = 
  yaml::Schema()
    .scalar("guid", yaml::Scalar::Int)
    .scalar("tag",  yaml::Scalar::String)
    .file("name")
    .path("path")
  ;

static const std::unordered_map<Resource::Tag, yaml::Schema> p_meta_schemas = {
  { Text::tag(),   yaml::Schema()
                             .scalar("location", yaml::Scalar::Tagged) },
  { Shader::tag(), yaml::Schema()
                             .scalarSequence("vertex",   yaml::Scalar::Tagged, yaml::Optional)
                             .scalarSequence("geometry", yaml::Scalar::Tagged, yaml::Optional)
                             .scalarSequence("fragment", yaml::Scalar::Tagged, yaml::Optional) },
  { Image::tag(),  yaml::Schema()
                             .scalar("location", yaml::Scalar::Tagged)
                             .scalar("channels", yaml::Scalar::String, yaml::Optional)
                             .scalar("flip_vertical", yaml::Scalar::Boolean, yaml::Optional)
                             .scalarSequence("dimensions", yaml::Scalar::Int) },
  { Texture::tag(),  yaml::Schema()
                             .scalar("location", yaml::Scalar::Tagged) },
  { Mesh::tag(),   yaml::Schema()
                             .scalar("location", yaml::Scalar::Tagged)
                             .mapping("vertex")
                             .scalar("indexed", yaml::Scalar::Boolean)
                             .scalar("primitive", yaml::Scalar::String) },
  { LookupTable::tag(), yaml::Schema()
                             .scalar("location", yaml::Scalar::Tagged)
                             .scalar("type", yaml::Scalar::String) },
};

const std::unordered_map<Resource::Tag, SimpleFsLoader::LoaderFn> SimpleFsLoader::loader_fns = {
  { Text::tag(),        &SimpleFsLoader::loadText    },
  { Shader::tag(),      &SimpleFsLoader::loadShader  },
  { Image::tag(),       &SimpleFsLoader::loadImage   },
  { Texture::tag(),     &SimpleFsLoader::loadTexture },
  { Mesh::tag(),        &SimpleFsLoader::loadMesh    },
  { LookupTable::tag(), &SimpleFsLoader::loadLUT     },
};

void SimpleFsLoader::doInit()
{
  os::current_working_directory(m_path.data());

  enumAvailable("./"); // enum all resources starting from 'base_path'
}

Resource::Ptr SimpleFsLoader::doLoad(Resource::Id id, LoadFlags flags)
{
  auto it = m_available.find(id);
  if(it == m_available.end()) return Resource::Ptr();  // Resource not found!

  // The file was already parsed (in enumOne()) so we can assume it is valid
  auto meta = yaml::Document::from_string(it->second.get<const char>(), it->second.size());
  if(p_meta_generic_schema.validate(meta)) throw InvalidResourceError(id);

  auto tag = ResourceManager::make_tag(meta("tag")->as<yaml::Scalar>()->str());
  if(!tag) throw InvalidResourceError(id);

  auto schema_it = p_meta_schemas.find(tag.value());
  assert(schema_it != p_meta_schemas.end() && "schema missing in p_meta_schemas!");

  if(schema_it->second.validate(meta)) throw InvalidResourceError(id);

  auto loader_it = loader_fns.find(tag.value());
  assert(loader_it != loader_fns.end() && "loading function missing in SimpleFsLoader!");

  auto loader = loader_it->second;

  return (this->*loader)(id, meta);
}

void SimpleFsLoader::enumAvailable(std::string path)
{
  using namespace std::literals::string_literals;

  // Snoop subdirectories first
  std::string dir_query_str = path + "*";
  auto dir_query = os::FileQuery::open(dir_query_str.data());

  dir_query().foreach([this,&path](const char *name, os::FileQuery::Attributes attrs) {
    // Have to manually check whether 'name' is a directory
    bool is_directory = attrs & os::FileQuery::IsDirectory;
    if(!is_directory) return;

    // 'name' is a directory - descend into it
    enumAvailable(path + name + "/"s);
  });

  // Now query *.meta files (resource descriptors)
  auto meta_query_str = path + "*.meta"s;
  auto meta_query = os::FileQuery::null();
  try {
    meta_query = os::FileQuery::open(meta_query_str.data());
  } catch(const os::FileQuery::Error&) {
    return; // no .meta files in current directory
  }

  meta_query->foreach([this,path](const char *name, os::FileQuery::Attributes attrs) {
    // if there's somehow a directory that fits *.meta ignore it
    if(attrs & os::FileQuery::IsDirectory) return;

    auto full_path = path + name;

    try {
      // The std::vector of IORequests must be a class member here, as otherwise
      //   resizing it will cause use-after-free bugs due to emplace_back()
      //   refernce invalidation semantics

      // TODO: A splash screen should be up on the screen here to let the user
      //       know the application is doing something and isn't just frozen
      auto& req = m_io_reqs.emplace_back(IORequest::read_file(full_path));
      req->onCompleted(
        std::bind(&SimpleFsLoader::metaIoCompleted,
          this, full_path, std::placeholders::_1)
      );

      manager().requestIo(req);
    } catch(const os::FileQuery::Error&) {
      // panic, there really shouldn't be an exception here
      os::panic(util::fmt("error opening file \"%s\"", full_path).data(), os::FileOpenError);
    }
  });

  // Wait until all the requests have completed
  manager().waitIoIdle();
}

void SimpleFsLoader::metaIoCompleted(std::string full_path, IORequest& req)
{
  auto file = req.result();

  auto id = enumOne(file.get<const char>(), file.size(), full_path.data());
  if(id == Resource::InvalidId) return;  // The *.meta file was invalid

  printf("found resource %-25s: 0x%.16lx\n", full_path.data(), id);

  // Multiple threads can be executing this method simultaneously
  auto guard = m_available_mutex->acquireScoped();
  m_available.emplace(
    id,
    std::move(file)
  );
}

Resource::Id SimpleFsLoader::enumOne(const char *meta_file, size_t sz, const char *full_path)
{
  yaml::Document meta;
  try {
    meta = yaml::Document::from_string(meta_file, sz);
  } catch(const yaml::Document::Error& e) {
    printf("warning: meta file '%s' could not be parsed\n", full_path);
    printf("         %s\n", e.what().data());
    return Resource::InvalidId;
  }

  auto guid_node = meta("guid");
  if(guid_node && guid_node->type() == yaml::Node::Scalar) {
    auto guid = guid_node->as<yaml::Scalar>();

    // make sure the node is a Scalar::Int
    if(guid->dataType() == yaml::Scalar::Int) return guid->ui();
  }

  printf("warning: meta file '%s' doesn't have a 'guid' node (or it's not a Scalar)\n", full_path);
  return Resource::InvalidId;
}

SimpleFsLoader::NamePathTuple SimpleFsLoader::name_path(const yaml::Document& meta)
{
  return std::make_tuple(
    meta("name")->as<yaml::Scalar>(),
    meta("path")->as<yaml::Scalar>()
  );
}

SimpleFsLoader::NamePathLocationTuple SimpleFsLoader::name_path_location(const yaml::Document& meta)
{
  return std::make_tuple(
    meta("name")->as<yaml::Scalar>(),
    meta("path")->as<yaml::Scalar>(),
    meta("location")->as<yaml::Scalar>()
  );
}

Resource::Ptr SimpleFsLoader::loadText(Resource::Id id, const yaml::Document& meta)
{
  // execution only reaches here when 'meta' has passed validation
  // so we can assume it's valid
  auto [name, path, location] = name_path_location(meta);

  auto req = IORequest::read_file(location->str());
  manager().requestIo(req);

  auto text = manager().waitIo(req);

  return Text::from_file(text.get<const char>(), text.size(), id, name->str(), path->str());
}

Resource::Ptr SimpleFsLoader::loadShader(Resource::Id id, const yaml::Document& meta)
{
  auto [name, path] = name_path(meta);

  return Shader::from_yaml(meta, id, name->str(), path->str());
}

static const std::map<std::string, unsigned> p_num_channels = {
  { "i",    1 },
  { "ia",   2 },
  { "rgb",  3 },
  { "rgba", 4 },
};

Resource::Ptr SimpleFsLoader::loadImage(Resource::Id id, const yaml::Document& meta)
{
  auto [name, path, location] = name_path_location(meta);

  auto channels_node = meta("channels");
  auto dims          = meta("dimensions");
  auto flip_vertical = meta("flip_vertical");

  unsigned num_channels = 0;
  if(channels_node) {
    auto channels = channels_node->as<yaml::Scalar>()->str();
    auto it = p_num_channels.find(channels);
    if(it == p_num_channels.end()) throw InvalidResourceError(id);

    num_channels = it->second;
  }

  auto width  = (int)dims->get<yaml::Scalar>(0)->i();
  auto height = (int)dims->get<yaml::Scalar>(1)->i();

  unsigned flags = 0;
  if(flip_vertical && flip_vertical->as<yaml::Scalar>()->b()) flags |= Image::FlipVertical;

  auto view = manager().mapLocation(location);
  if(!view) throw IOError(location->repr());

  auto img = Image::from_file(view.get(), view.size(), num_channels, flags, id, name->str(), path->str());

  if(img->as<Image>()->dimensions() != ivec2{ width, height }) {
    throw InvalidResourceError(id);
  }

  return img;
}

Resource::Ptr SimpleFsLoader::loadTexture(Resource::Id id, const yaml::Document& meta)
{
  auto [name, path, location] = name_path_location(meta);

  // Texture copies the data so map it to
  //   prevent unnecessary copying
  auto view = manager().mapLocation(location);
  if(!view) throw IOError(location->repr());

  return Texture::from_yaml(view, meta, id, name->str(), path->str());
}

Resource::Ptr SimpleFsLoader::loadMesh(Resource::Id id, const yaml::Document& meta)
{
  auto [name, path, location] = name_path_location(meta);

  auto req = IORequest::read_file(location->str());
  manager().requestIo(req);

  auto& mesh_data = manager().waitIo(req);

  return Mesh::from_yaml(std::move(mesh_data), meta, id, name->str(), path->str());
}

Resource::Ptr SimpleFsLoader::loadLUT(Resource::Id id, const yaml::Document& meta)
{
  auto [name, path, location] = name_path_location(meta);

  // Read in the file, because the LookupTable stores
  //   the IOBuffer with the data directly
  auto req = IORequest::read_file(location->str());
  manager().requestIo(req);

  auto& lut_data = manager().waitIo(req);

  return LookupTable::from_yaml(std::move(lut_data), meta, id, name->str(), path->str());
}

}
