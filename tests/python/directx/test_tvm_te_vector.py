import ctypes
import sys, os, pathlib
import numpy as np


def walk_dir(file_dir):
    L = []
    for root, dirs, files in os.walk(file_dir):
        for file in files:
            if file == "tvm.dll":
                # if file == "libtvm.dylib":
                L.append(os.path.join(root, file))
    return L


def find_tvmlib(dir):
    L = walk_dir(str(dir))
    assert len(L) == 1  # There should be only one tvm.dll in build folder, either Release or Debug.
    return pathlib.Path(L[0]).parent.resolve()


def init():
    cur_dir = pathlib.Path(__file__).parent.resolve()
    tvm_dir = pathlib.Path(os.path.join(cur_dir, "../../../python")).resolve()
    tvm_home_dir = pathlib.Path(os.path.join(cur_dir, "../../../")).resolve()
    lib_dir = find_tvmlib(tvm_home_dir)
    print("TVM home dir: " + str(tvm_home_dir))
    print("TVM lib dir: " + str(lib_dir))
    print("TVM python dir: " + str(tvm_dir))
    sys.path.append(str(lib_dir))
    sys.path.append(str(tvm_dir))
    os.environ["PATH"] = os.environ["PATH"] + "; " + str(lib_dir) + "; " + str(tvm_dir)
    os.environ["TVM_HOME"] = str(tvm_home_dir)
    os.environ["PYTHONPATH"] = str(tvm_dir)
    print("------------")


init()


def test_vector_add():
    import tvm
    # Create vector add operator
    # Shape has to be const if no llvm installed.
    n = 127
    i = tvm.runtime.convert(n)
    A = tvm.te.placeholder((i,), name="A")
    B = tvm.te.placeholder((i,), name="B")
    C = tvm.te.compute(A.shape, lambda i: A[i] + B[i], name="C")
    # Create Schedule
    s = tvm.te.create_schedule(C.op)
    bx, tx = s[C].split(C.op.axis[0], factor=64)
    s[C].bind(bx, tvm.te.thread_axis("blockIdx.x"))
    s[C].bind(tx, tvm.te.thread_axis("threadIdx.x"))
    # Assign target
    target = "directx"
    fadd = tvm.build(s, [A, B, C], target)
    print(fadd.imported_modules[0].get_source())
    # Creat input/output
    dev = tvm.device(target, 0)
    a = tvm.nd.array(np.random.uniform(size=n).astype(A.dtype), dev)
    b = tvm.nd.array(np.random.uniform(size=n).astype(B.dtype), dev)
    c = tvm.nd.array(np.zeros(n, dtype=C.dtype), dev)
    # Forward
    fadd(a,b,c)
    # Get output
    np.testing.assert_allclose(c.numpy(), a.numpy() + b.numpy())
    print("C = A + B ... OK")


def main():
    test_vector_add()


if __name__ == "__main__":
    main()
