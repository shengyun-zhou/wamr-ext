#include <argparse/argparse.hpp>
#include <wamr_ext_api.h>
#include <cstring>

int main(int argc, char** argv) {
    argparse::ArgumentParser ap("wamr_ext_miniapp");
    ap.add_argument("--max-threads").help("maximum thread number").scan<'i', int>().default_value(0);
    ap.add_argument("file_and_args").help("Wasm app file to load and arguments passed to main() of the wasm app").remaining();
    try {
        ap.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << ap;
        std::exit(1);
    }
    int32_t maxThreadNum = ap.get<int>("--max-threads");
    std::vector<std::string> progArgs = ap.get<std::vector<std::string>>("file_and_args");
    std::string wasmAppFile = progArgs.front();
    char** mainArgv = new char*[progArgs.size() + 1];
    for (size_t i = 0; i < progArgs.size(); i++)
        mainArgv[i] = const_cast<char*>(progArgs[i].c_str());
    mainArgv[progArgs.size()] = nullptr;

    wamr_ext_init();
    wamr_ext_module_t module;
    int err = wamr_ext_module_load_by_file(&module, wasmAppFile.c_str());
    if (err != 0) {
        printf("Failed to load wasm app %s: %s\n", wasmAppFile.c_str(), wamr_ext_strerror(err));
        return err;
    }
    if (maxThreadNum > 0)
        wamr_ext_module_set_inst_default_opt(&module, WAMR_EXT_INST_OPT_MAX_THREAD_NUM, &maxThreadNum);
    wamr_ext_instance_t inst;
    wamr_ext_instance_create(&module, &inst);
    wamr_ext_instance_set_opt(&inst, WAMR_EXT_INST_OPT_ARG, mainArgv);
    err = wamr_ext_instance_start(&inst);
    if (err != 0)
        printf("Failed to start wasm app: %s\n", wamr_ext_strerror(err));
    delete[] mainArgv;
    return err;
}