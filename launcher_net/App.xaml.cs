using System.Windows;

namespace UnchartedLauncher;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
#if NETFRAMEWORK
        // Windows 7 / 8 .NET 4.8 clients need TLS 1.2 opted in before calling GitHub HTTPS endpoints.
        System.Net.ServicePointManager.SecurityProtocol |= System.Net.SecurityProtocolType.Tls12;
#endif
        base.OnStartup(e);
    }
}
