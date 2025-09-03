import common
import pytest
import sys
import os

# OK, I admit I don't understand this fixture/parameterization stuff
# What I do understand is that openssl crashes when running with too many key_exchange algs
# hence this crude hack to do two tests with half the KEXs each
# XXX anyone better at Python/pytest please improve this!

@pytest.fixture(params=common.signatures)
def server0(ossl, ossl_config, test_artifacts_dir, request, worker_id):
    # Setup: start ossl server
    common.set_kex_sig(ossl)
    common.gen_keys(ossl, ossl_config, request.param, test_artifacts_dir, worker_id)
    server, port = common.start_server(ossl, test_artifacts_dir, request.param, worker_id, 0)
    # Run tests
    yield (request.param, port)
    # Teardown: stop ossl server
    server.kill()

@pytest.fixture(params=common.signatures)
def server1(ossl, ossl_config, test_artifacts_dir, request, worker_id):
    # Setup: start ossl server
    common.set_kex_sig(ossl)
    common.gen_keys(ossl, ossl_config, request.param, test_artifacts_dir, worker_id)
    server, port = common.start_server(ossl, test_artifacts_dir, request.param, worker_id, 1)
    # Run tests
    yield (request.param, port)
    # Teardown: stop ossl server
    server.kill()

@pytest.mark.parametrize('kex_name', common.key_exchanges[:len(common.key_exchanges)//2])
def test_sig_kem_pair(ossl, server0, test_artifacts_dir, kex_name, worker_id):
    client_output = common.run_subprocess([ossl, 's_client',
                                                  '-groups', kex_name,
                                                  '-CAfile', os.path.join(test_artifacts_dir, '{}_{}_CA.crt'.format(worker_id, server0[0])),
                                                  '-verify_return_error',
                                                  '-connect', 'localhost:{}'.format(server0[1])],
                                    input='Q'.encode())
# OpenSSL3 by default does not output KEM used; so rely on forced client group and OK handshake completion:
    if not "SSL handshake has read" in client_output:
        assert False, "Handshake failure."

@pytest.mark.parametrize('kex_name', common.key_exchanges[len(common.key_exchanges)//2:])
def test_sig_kem_pair(ossl, server1, test_artifacts_dir, kex_name, worker_id):
    client_output = common.run_subprocess([ossl, 's_client',
                                                  '-groups', kex_name,
                                                  '-CAfile', os.path.join(test_artifacts_dir, '{}_{}_CA.crt'.format(worker_id, server1[0])),
                                                  '-verify_return_error',
                                                  '-connect', 'localhost:{}'.format(server1[1])],
                                    input='Q'.encode())
# OpenSSL3 by default does not output KEM used; so rely on forced client group and OK handshake completion:
    if not "SSL handshake has read" in client_output:
        assert False, "Handshake failure."

if __name__ == "__main__":
    import sys
    pytest.main(sys.argv)
