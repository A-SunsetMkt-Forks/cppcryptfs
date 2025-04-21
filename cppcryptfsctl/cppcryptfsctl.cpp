/*
cppcryptfs : user-mode cryptographic virtual overlay filesystem.

Copyright (C) 2016-2025 Bailey Brown (github.com/bailey27/cppcryptfs)

cppcryptfs is based on the design of gocryptfs (github.com/rfjakob/gocryptfs)

The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <windows.h>
#include <Shlwapi.h>
#include <fcntl.h>
#include <iostream>
#include <io.h>
#include <string>
#include <vector>
#include "../libipc/client.h"
#include "../libipc/certutil.h"
#include "../libcommonutil/commonutil.h"
#include "../libcppcryptfs/config/cryptconfig.h"
#include "../libcppcryptfs/util/getopt.h"
#include "../libcppcryptfs/util/util.h"
#include "../libcppcryptfs/util/fileutil.h"
#include "../libcppcryptfs/crypt/cryptdefs.h"

#pragma comment(lib, "Ws2_32.lib")

using namespace std;


#define PASSWORD_BUFLEN (MAX_PASSWORD_LEN+5)

static void show_help()
{
    wcerr << get_command_line_usage();
}


static int get_binary_flag(const char *option, const wchar_t* s, bool& f)
{

    if (wcscmp(s, L"0") == 0 || wcscmp(s, L"1") == 0) {
        f = *s == L'1';
        return 0;
    } else {
        wcerr << L"argument must be 0 or 1 for option " << option << endl;
        return -1;
    }
        
}

static int get_password(LockZeroBuffer<wchar_t>& password, const wchar_t *password_prompt, const wchar_t *repeat_prompt = nullptr) 
{
    if (_isatty(_fileno(stdin))) {

        LockZeroBuffer<wchar_t> password2(PASSWORD_BUFLEN, false);

        if (!password2.IsLocked()) {
            wcerr << L"unable to lock repeat password buffer\n";
            return 1;
        }

        // prompt for password
        if (!read_password(password.m_buf, password.m_len, password_prompt)) {
            wcerr << L"error reading password" << endl;
            return 1;
        }
        if (repeat_prompt) {
            // prompt for repeat password
            if (!read_password(password2.m_buf, password2.m_len, repeat_prompt)) {
                wcerr << L"error reading repeat password" << endl;
                return 1;
            }
            if (wcscmp(password.m_buf, password2.m_buf) != 0) {
                wcerr << L"passwords do not match" << endl;
                return 1;
            }
        }
    } else {
        // we have stdin redirected, so read password from stdin         
        std::wcout << L"Reading password from stdin" << endl;
        if (!fgetws(password.m_buf, password.m_len, stdin)) {
            wcerr << "unable to read password from stdin\n";
            return 1;
        }
        if (wcslen(password.m_buf) > 0 && password.m_buf[wcslen(password.m_buf) - 1] == L'\n') {
            password.m_buf[wcslen(password.m_buf) - 1] = L'\0';
        }
    }

    return 0;
}

static void GetConfigPath(wstring& path)
{
    if (::PathIsDirectory(path.c_str())) {
        if (path[path.length() - 1] != L'\\') {
            path += L"\\";
        }
        if (::PathFileExists((path + L"gocryptfs.conf").c_str())) {
            path += L"gocryptfs.conf";
        } else if (::PathFileExists((path + L".gocryptfs.reverse.conf").c_str())) {
            path += L".gocryptfs.reverse.conf";
        }
    }
}

static bool is_hex(wint_t c) 
{
    return iswdigit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

static int do_self_args(int argc, wchar_t* const argv[])
{    
    CryptConfig config;

    // need to intialize these getop variables before we process a command line
    getopt_init();

    int c;
    int option_index = 0;

    bool invalid_opt = false;

    bool reverse = false;

    bool plaintext_names = false;

    bool longnames = true;

    bool streams = true;

    bool do_init = false;

    bool siv = false;

    bool do_version = false;

    bool do_help = false;

    bool do_changepassword = false;

    bool do_printmasterkey = false;

    bool do_recover = false;

    bool deterministicnames = false;

    int scryptn = 0;

    LockZeroBuffer<wchar_t> password(PASSWORD_BUFLEN, false);

    if (!password.IsLocked()) {
        wcerr << L"unable to lock password buffer\n";
        return 1;
    }

    wstring fs_path;
    wstring change_password_path;
    wstring print_masterkey_path;
    wstring recover_path;
    wstring config_path;
    wstring volume_name;

    int longnamemax = MAX_LONGNAMEMAX;

    static struct option long_options[] =
    {
        {L"init",   required_argument,  0, 'I'},  
        {L"deterministicnames",   no_argument,  0, 'd'},
        {L"config", required_argument, 0, 'c' },      
        {L"reverse",  no_argument, 0, 's' },
        {L"plaintextnames",  no_argument, 0, 'T' },
        {L"longnames",  required_argument, 0, 'L' },        
        {L"streams",  required_argument, 0, 'b'},
        {L"volumename",  required_argument, 0, 'V'},
        {L"siv",  no_argument, 0, 'S'},        
        {L"version",  no_argument, 0, 'v' },
        {L"help",  no_argument, 0, 'h'},
        {L"password", required_argument, 0, 'p'},
        {L"changepassword",   required_argument,  0, '0'},
        {L"printmasterkey",   required_argument,  0, '1'},
        {L"recover",   required_argument,  0, '2'},
        {L"longnamemax",  required_argument, 0, '3' },
        {L"scryptn",  required_argument, 0, '4' },
        {0, 0, 0, 0}
    };

    while (true) {
        c = getopt_long(argc, argv, L"p:dI:c:sTL:b:V:Svh0:1:2:3:4:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
        case '?':
            invalid_opt = true;
            break;
        case '0':
            do_changepassword = true;
            change_password_path = optarg;
            break;
        case '1':
            do_printmasterkey = true;
            print_masterkey_path = optarg;
            break;
        case '2':
            do_recover = true;
            recover_path = optarg;
            break;
        case '3':
            longnamemax = _wtoi(optarg);
            break;
        case '4':
            scryptn = _wtoi(optarg);
            break;
        case 'p':
            if (wcslen(optarg) > MAX_PASSWORD_LEN) {
                wcerr << L"password too long.  max length is " << MAX_PASSWORD_LEN << endl;
                return 1;
            }
            wcscpy_s(password.m_buf, MAX_PASSWORD_LEN + 1, optarg);
            break;
        case 'I':
            if (wcscmp(optarg, L"-v") == 0) {
                do_version = true;
            } else if (wcscmp(optarg, L"-h") == 0) {
                do_help = true;
            } else {
                do_init = true;
                fs_path = optarg;
            }
            break;
        case 'd':
            deterministicnames = true;
            break;
        case 'c':
            config_path = optarg;
            break;
        case 's':
            reverse = true;
            break;
        case 'T':
            plaintext_names = true;
            break;
        case 'L':
            if (get_binary_flag("longnames", optarg, longnames) != 0)
                return 1;
            break;           
        case 'b':
            if (get_binary_flag("streams", optarg, streams) != 0)
                return 1;
            break;
        case 'V':
            volume_name = optarg;
            break;
        case 'S':
            siv = true;
            break;
        case 'v':
            do_version = true;
            break;
        case 'h':
            do_help = true;
            break;      
        }
    }    

    if (scryptn != 0)
    {
        if (scryptn < MIN_SCRYPTN || scryptn > MAX_SCRYPTN) {
            wcerr << L"Invalid scryptn specified.  Valid range is " << to_wstring(MIN_SCRYPTN) << " to " << to_wstring(MAX_SCRYPTN) << "." << endl;
            return 1;
        }
    }

    if (longnamemax != MAX_LONGNAMEMAX && plaintext_names) {
        wcerr << L"Invalid parmameter combination: longnamemax and plain text filenames\n";
        return 1;
    }

    if (longnamemax != MAX_LONGNAMEMAX && !longnames) {
        wcerr << L"Invalid parmameter combination: longnamemax and no longnames\n";
        return 1;
    }

    if (invalid_opt && !do_help) {
        wcerr << L"Invalid option. Try 'cppcryptfsctl --help' for more information." << endl;
        return 1;
    }    

    if (deterministicnames) {
        if (plaintext_names) {
            wcerr << L"Using deterministic names and plaintext names together doesn't make sense." << endl;
            return 1;
        }
        if (!do_init) {
            wcerr << L"Deterministic names makes sense only when combined with --init." << endl;
            return 1;
        }
    }

    if (do_version) {
        wstring prod, ver, copyright;
        GetProductVersionInfo(prod, ver, copyright);
        wcerr << prod << " " << ver << " " << copyright << endl;
    }     

    int opcount = do_recover + do_printmasterkey + do_changepassword + do_init;

    if (opcount > 1) {
        wcerr << L"only one of recover, printmasterkey, changepassword or init can be specified at the same time" << endl;
        return 1;
    }

    if (do_help || (!do_recover && !do_printmasterkey && !do_changepassword && !do_init && !do_version)) {       
        show_help();
        return 1;
    }
 
    wstring mes;

    if (do_init) {

        if (!::PathFileExists(fs_path.c_str())) {
            wcerr << L"the path to the file system does not exist." << endl;
            return 1;
        }

        if (!reverse && !can_delete_directory(fs_path.c_str())) {
            wcerr << L"the file system directory is not empty." << endl;
            return 1;
        }               

        if (wcslen(password.m_buf) < 1) {
            std::wcout << L"Choose a password for protecting your files." << endl;

            auto pw_res = get_password(password, L"Password:", L"Repeat:");

            if (pw_res) {
                return pw_res;
            }
        }

        if (wcslen(password.m_buf) < 1) {
            wcerr << L"password cannot be empty" << endl;
            return 1;
        }

        if (wcslen(password.m_buf) > MAX_PASSWORD_LEN) {
            wcerr << L"password too long.  max length is " << MAX_PASSWORD_LEN << endl;
            return 1;
        }        
    
        bool result = config.create(fs_path.c_str(), config_path.c_str(), password.m_buf, !plaintext_names, plaintext_names, longnames, reverse || siv, reverse, scryptn ? scryptn : DEFAULT_SCRYPTN, volume_name.c_str(), !streams, longnamemax, deterministicnames, mes);

        if (!result) {
            wcerr << mes << endl;
            return 1;
        } else {
            std::wcout << L"The gocryptfs" << (reverse ? L"-reverse" : L"") <<  L" filesystem has been created successfully." << endl;
        }
    }

    if (do_recover) {
        if (recover_path.length() < 1) {
            wcerr << L"path cannot be empty\n";
            return 1;
        }

        GetConfigPath(recover_path);


        CryptConfig config;

        wstring mes;

        if (!config.read(mes, recover_path.c_str())) {
            wcerr << mes << endl;
            return 1;
        }        

        wstring bak = recover_path + L".bak";
        if (::PathFileExists(bak.c_str())) {
            wcerr << bak << L" exists.  Please delete or move out of the way." << endl;
            return 1;
        }       

        LockZeroBuffer<wchar_t> masterkey(PASSWORD_BUFLEN, false);
        if (!masterkey.IsLocked()) {
            wcerr << L"unable to lock masterkey buffer\n";
            return 1;
        }

        std::wcout << L"Enter/paste master key all on one line, with or without dashes." << endl;

        auto get_res = get_password(masterkey, L"Master Key:");
        if (get_res) {
            return get_res;
        }

        LockZeroBuffer<BYTE> masterkey_bin(DEFAULT_KEY_LEN, false);

        if (!masterkey_bin.IsLocked()) {
            wcerr << L"unable to lock masterkey binary buffer\n";
            return 1;
        }

        size_t i = 0;
        size_t j = 0;        
        size_t mklen = wcslen(masterkey.m_buf);
        while (i < DEFAULT_KEY_LEN && j < (mklen - 1)) {
            if (!is_hex(masterkey.m_buf[j])) {
                ++j;
                continue;
            }
            if (is_hex(masterkey.m_buf[j]) && is_hex(masterkey.m_buf[j + 1])) {
                char buf[3];
                buf[0] = static_cast<char>(masterkey.m_buf[j]);
                buf[1] = static_cast<char>(masterkey.m_buf[j+1]);
                buf[2] = '\0';
                unsigned int b;
                sscanf_s(buf, "%x", &b);
                masterkey_bin.m_buf[i++] = static_cast<unsigned char>(b);
                j += 2;
            }
        }

        if (i != DEFAULT_KEY_LEN) {
            wcerr << L"invalid master key\n";
            return 1;
        }

        LockZeroBuffer<wchar_t> newpassword(PASSWORD_BUFLEN, false);
        if (!newpassword.IsLocked()) {
            wcerr << L"unable to lock new password buffer\n";
            return 1;
        }

        auto get_pw_res = get_password(newpassword, L"New Password:", L"Repeat:");
        if (get_pw_res) {
            return get_pw_res;
        }

        CryptConfig dummyConfig;

        dummyConfig.CopyKeyParams(config);     

        if (scryptn != 0)
            dummyConfig.m_N = 1 << scryptn;

        string base64key;
        string scryptSalt;
        if (!dummyConfig.encrypt_key(newpassword.m_buf, masterkey_bin.m_buf, base64key, scryptSalt, mes)) {
            wcerr << mes << endl;
            return 1;
        }

        if (!::CopyFile(recover_path.c_str(), bak.c_str(), TRUE)) {
            wcerr << L"unable to backup " << recover_path << endl;
            return 1;
        }

        if (config.m_VolumeName.length() > 0) {
            wcerr << L"warning: ignoring volume name in recovery config file." << endl;           
        }
        if (!config.write_updated_config_file(base64key.c_str(), scryptSalt.c_str(), scryptn)) {
            wcerr << L"failed to update encrypted key" << endl;
            return 1;
        }

        std::wcout << L"key encrypted with new password written to " << recover_path << endl;
        std::wcout << L"after mounting and testing, please delete or move the backup file " << bak << L" out of the way." << endl;

    }

    if (do_printmasterkey) {

        if (print_masterkey_path.length() < 1) {
            wcerr << L"path cannot be empty\n";
            return 1;
        }

        GetConfigPath(print_masterkey_path);

        CryptConfig config;

        wstring mes;

        if (!config.read(mes, print_masterkey_path.c_str())) {
            wcerr << mes << endl;
            return 1;
        }

        auto get_pw_res = get_password(password, L"Password:");

        if (get_pw_res) {
            return get_pw_res;
        }

        if (!config.decrypt_key(password.m_buf)) {
            wcerr << L"password incorrect" << endl;
            return 1;
        }

        const unsigned char* key = config.GetMasterKey();

        std::wcout << endl << L"Your master key is as follows.  Keep it in a safe place." << endl << endl << "    ";

        for (size_t i = 0; i < config.GetMasterKeyLength(); ++i) {
            if (i && (i % 4) == 0) {
                std::wcout << L"-";
            }
            if (i && (i % 16) == 0) {
                std::wcout << endl << L"    ";
            }
            wchar_t buf[3];
            swprintf_s(buf, L"%02x", key[i]);    
            std::wcout << buf;
        }
        std::wcout << endl;

    }

    if (do_changepassword) {

        if (change_password_path.length() < 1) {
            wcerr << L"path cannot be empty\n";
            return 1;
        }
        
        GetConfigPath(change_password_path);

        std::wcout << L"changing password in " << change_password_path << endl;

        CryptConfig config;

        wstring mes;

        if (!config.read(mes, change_password_path.c_str())) {
            wcerr << mes << endl;
            return 1;
        }

        if (0 && !config.m_HKDF) {
            wcerr << L"This filesystem is not using HKDF. Unable to proceeed." << endl;
            return 1;
        }

        LockZeroBuffer<wchar_t> newpassword(PASSWORD_BUFLEN, false);
        if (!newpassword.IsLocked()) {
            wcerr << L"unable to lock new password buffer\n";
            return 1;
        }

        auto get_pw_res = get_password(password, L"Password:");

        if (get_pw_res) {
            return get_pw_res;
        }

        if (!config.decrypt_key(password.m_buf)) {
            wcerr << L"password incorrect" << endl;
            return 1;
        }

        get_pw_res = get_password(newpassword, L"New Password:", L"Repeat:");
        if (get_pw_res) {
            return get_pw_res;
        }

        CryptConfig dummyConfig;

        dummyConfig.CopyKeyParams(config);

        if (scryptn != 0)
            dummyConfig.m_N = 1 << scryptn;

        string base64key;
        string scryptSalt;

        if (scryptn != 0)
            dummyConfig.m_N = 1 << scryptn;
        
        if (!dummyConfig.encrypt_key(newpassword.m_buf, config.GetMasterKey(), base64key, scryptSalt, mes)) {
            wcerr << mes << endl;
            return 1;
        }   

        if (!config.write_updated_config_file(base64key.c_str(), scryptSalt.c_str(), scryptn)) {
            wcerr << "failed to update encrypted key" << endl;
            return 1;
        }

        std::wcout << L"password changed" << endl;
    }

    return 0;
}


int wmain(int argc, wchar_t * const argv[])
{
    if (argc < 2)
        return 0;

    // Set output mode of stdout and stderr to UTF-16
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    vector<wstring> self_args;
   
    self_args.push_back(L"-h");
    self_args.push_back(L"--help");
    self_args.push_back(L"-I");
    self_args.push_back(L"--init");
    self_args.push_back(L"-0");
    self_args.push_back(L"--changepassword");
    self_args.push_back(L"-1");
    self_args.push_back(L"--printmasterkey");
    self_args.push_back(L"-2");
    self_args.push_back(L"--recover");

    vector<wstring> mounting_args;

    mounting_args.push_back(L"-m");
    mounting_args.push_back(L"--mount");

    vector<wstring> password_args;

    password_args.push_back(L"-p");
    password_args.push_back(L"--password");

    // if we are doing certain things like initializing a filesystem then we handle
    // it in cppcryptfsctl instead of passing the command line to cppcryptfs

    bool have_mounting_arg = false;

    bool have_password_arg = false;

    for (int i = 1; i < argc; ++i) {
        for (const auto& self_arg : self_args) {
            if (wcsncmp(argv[i], self_arg.c_str(), self_arg.length()) == 0) {
                return do_self_args(argc, argv);
            }
        }    
        if (!have_mounting_arg) {        
            for (const auto& arg : mounting_args) {
                if (wcsncmp(argv[i], arg.c_str(), arg.length()) == 0) {
                    have_mounting_arg = true;
                    break;
                }
            }
        }
        if (!have_password_arg) {
            for (const auto& arg : password_args) {
                if (wcsncmp(argv[i], arg.c_str(), arg.length()) == 0) {
                    have_password_arg = true;
                    break;
                }
            }
        }
    }

    LockZeroBuffer<wchar_t> password(PASSWORD_BUFLEN, false);

    if (!password.IsLocked()) {
        wcerr << L"unable to lock password buffer\n";
        return 1;
    }

    // if we're trying to mount and don't have -p or --password then read pw from stdin
    if (have_mounting_arg && !have_password_arg) {
        
        auto pw_res = get_password(password, L"Password:", nullptr);

        if (pw_res) {
            return pw_res;
        }

    }

    wstring result;

    wstring err;

    if (argc == 2 && !lstrcmp(argv[1], L"-V")) {
        wstring prodname, prodver, copyright;
        if (GetProductVersionInfo(prodname, prodver, copyright)) {
            const wchar_t* bits = sizeof(void*) == 8 ? L" 64-bit " : L" 32-bit ";
            wcerr << prodname + L" " + prodver + bits + copyright << endl;
        } else {
            wcerr << wstring(argv[0]) + L" error getting version" << endl;
        }
        return 1;
    }

    const WCHAR* args = GetCommandLine();

    auto cmd_len = static_cast<DWORD>(wcslen(args) + wcslen(L" --password=") + wcslen(password.m_buf) + 1);

    LockZeroBuffer<wchar_t> cmd(cmd_len, false);

    if (!cmd.IsLocked()) {
        wcerr << L"unable to lock cmd buffer" << endl;
        return 1;
    }

    wcscpy_s(cmd.m_buf, cmd_len, args);

    if (wcslen(password.m_buf) > 0) {
        wcscat_s(cmd.m_buf, cmd_len, L" --password=");
        wcscat_s(cmd.m_buf, cmd_len, password.m_buf);
    }
    
    if (auto ret = SendArgsToRunningInstance(cmd.m_buf, result, err)) {
        if (err.length() > 0)
            wcerr << err << endl;
        else
            wcerr << L"cppcryptfsctl: Unable to send command." << endl;
        return ret;
    }

    if (result.length() >= CMD_PIPE_RESPONSE_LENGTH) {
        if (wcsncmp(result.c_str(), CMD_PIPE_SUCCESS_STR, CMD_PIPE_RESPONSE_LENGTH) == 0) {
            std::wcout << wstring(result.c_str() + CMD_PIPE_RESPONSE_LENGTH);
            return 0;
        } else {
            wcerr << wstring(result.c_str() + CMD_PIPE_RESPONSE_LENGTH);
            return 1;
        }
    } else {
        wcerr << "cppcryptfsctl: got a mal-formed response from cppcryptfs\n";
        return 1;
    }
}

