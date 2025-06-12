using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Concurrent;
using System.Collections.Generic;

public sealed class NetworkClient : IDisposable
{
    private readonly TcpClient _tcp = new();
    private StreamReader _reader;
    public  StreamWriter Writer { get; private set; }

    private readonly CancellationTokenSource _cts = new();

    /* Boîte centrale : toujours remplie par le ReadLoop ---------------- */
    private readonly BlockingCollection<string> _centralInbox = new();
    public  BlockingCollection<string>  Inbox => _centralInbox;

    /* Listes de queues abonnées par les écrans ------------------------- */
    private readonly List<ConcurrentQueue<string>> _sinks = new();
    private readonly object _sync = new();
    private bool _pumpStarted;

    /* ===== Connexion : le pump démarre immédiatement ================== */
    public async Task<bool> ConnectAsync(string host, int port)
    {
        try
        {
            await _tcp.ConnectAsync(host, port);
            var stream  = _tcp.GetStream();
            _reader     = new StreamReader(stream, Encoding.UTF8);
            Writer      = new StreamWriter(stream, Encoding.UTF8) { AutoFlush = true };

            StartPumpIfNeeded();            // garantit le ReadLoop
            return true;
        }
        catch
        {
            return false;
        }
    }

    /* ===== Abonnement d’une file ====================================== */
    public void Subscribe(ConcurrentQueue<string> q)
    {
        lock (_sync)
        {
            _sinks.Add(q);
            StartPumpIfNeeded();            // au cas où
        }
    }

    /* ===== Vidage utilitaire (match -> lobby) ========================= */
    public void ClearInbox()
    {
        while (_centralInbox.TryTake(out _)) { /* discard */ }
    }

    /* ===== Démarrage unique du ReadLoop =============================== */
    private void StartPumpIfNeeded()
    {
        if (_pumpStarted) return;
        _pumpStarted = true;
        _ = Task.Run(ReadLoopAsync, _cts.Token);
    }

    /* ===== Lecture socket → Inbox + sinks ============================= */
    private async Task ReadLoopAsync()
    {
        try
        {
            while (!_cts.Token.IsCancellationRequested)
            {
                var line = await _reader.ReadLineAsync();
                if (line == null) break;           // socket fermée

                _centralInbox.Add(line);

                lock (_sync)
                    foreach (var q in _sinks)
                        q.Enqueue(line);
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            Console.WriteLine($"[NetworkClient] ReadLoop stopped: {ex.Message}");
        }
    }

    /* ===== Utilitaires divers ========================================= */
    public void SendLine(string line) => Writer.WriteLine(line);

    public void Dispose()
    {
        _cts.Cancel();
        _tcp.Close();
        _centralInbox.CompleteAdding();
    }
}
