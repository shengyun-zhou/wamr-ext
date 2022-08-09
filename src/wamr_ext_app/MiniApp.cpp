#include <argparse/argparse.hpp>
#include <wamr_ext_api.h>
#include <cstring>

int main(int argc, char** argv) {
    argparse::ArgumentParser ap("wamr_ext_miniapp");
    ap.add_argument("file").help("Wasm file to load");
    ap.add_argument("--max-threads").help("maximum thread number").scan<'i', int>().default_value(0);
    ap.add_argument("args").help("arguments passed to main() of the wasm app").remaining();
    try {
        ap.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << ap;
        std::exit(1);
    }
    std::string wasmAppFile = ap.get<std::string>("file");
    int32_t maxThreadNum = ap.get<int>("--max-threads");
    std::vector<std::string> progArgs;
    try {
        progArgs = ap.get<std::vector<std::string>>("args");
    } catch (std::logic_error& e) {}
    char** mainArgv = new char*[progArgs.size() + 1];
    mainArgv[0] = const_cast<char*>(wasmAppFile.c_str());
    for (size_t i = 0; i < progArgs.size(); i++)
        mainArgv[i + 1] = const_cast<char*>(progArgs[i].c_str());

    wamr_ext_init();
    wamr_ext_module_t module;
    int err = wamr_ext_module_load_by_file(&module, wasmAppFile.c_str());
    if (err != 0) {
        printf("Failed to load wasm app %s: %s\n", wasmAppFile.c_str(), wamr_ext_strerror(err));
        return err;
    }
    if (maxThreadNum > 0)
        wamr_ext_module_set_inst_default_opt(&module, WAMR_INST_OPT_MAX_THREAD_NUM, &maxThreadNum);
    wamr_ext_instance_t inst;
    wamr_ext_instance_create(&module, &inst);
    err = wamr_ext_instance_init(&inst);
    if (err == 0) {
        err = wamr_ext_instance_run_main(&inst, progArgs.size() + 1, mainArgv);
        if (err != 0)
            printf("Failed to execute main() of wasm app: %s\n", wamr_ext_strerror(err));
    } else {
        printf("Failed to initialize the instance of wasm app: %s\n", wamr_ext_strerror(err));
    }
    delete[] mainArgv;
    return err;
}