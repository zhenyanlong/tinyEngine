
#include "mainWindows.h"
#include "Application.hpp"

using namespace std;

/**
 * @brief 程序入口：构造 Application，调用 run() 进入 GLFW + Vulkan 主循环。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组（支持 --mcp, --port N, --exit-after N）
 * @return 成功为 0；捕获到 std::exception 时打印信息后返回 -1
 */
int main(int argc, char* argv[])
{
	Application app;
	try {
		app.run(argc, argv);
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return -1;
	}

	return 0;
}
