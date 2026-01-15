RAW_DATA = """
Testing pattern_1... EL: 3.3±1.2ms Opt-CL: 1.9±0.5ms Opt-PM: 1.8±0.2ms Opt-CB: 1.8±0.2ms
Testing pattern_2... EL: 3.2±0.5ms Opt-CL: 2.1±0.2ms Opt-PM: 2.1±0.2ms Opt-CB: 2.4±0.5ms
Testing pattern_3... EL: 3.9±0.2ms Opt-CL: 2.1±0.1ms Opt-PM: 2.2±0.2ms Opt-CB: 2.2±0.2ms
Testing pattern_4... EL: 4.0±0.2ms Opt-CL: 2.3±0.2ms Opt-PM: 2.3±0.1ms Opt-CB: 2.3±0.2ms
Testing pattern_5... EL: 4.4±0.5ms Opt-CL: 2.5±0.1ms Opt-PM: 2.5±0.1ms Opt-CB: 2.6±0.1ms
Testing pattern_6... EL: 5.1±0.7ms Opt-CL: 2.6±0.1ms Opt-PM: 2.7±0.3ms Opt-CB: 2.5±0.2ms
Testing pattern_7... EL: 5.5±0.9ms Opt-CL: 2.8±0.1ms Opt-PM: 2.9±0.1ms Opt-CB: 2.7±0.1ms
Testing pattern_8... EL: 6.2±0.2ms Opt-CL: 3.0±0.2ms Opt-PM: 3.0±0.1ms Opt-CB: 3.0±0.1ms
Testing pattern_9... EL: 7.3±0.4ms Opt-CL: 3.2±0.1ms Opt-PM: 3.2±0.1ms Opt-CB: 3.2±0.1ms
Testing pattern_10... EL: 9.8±0.3ms Opt-CL: 3.7±0.2ms Opt-PM: 3.6±0.1ms Opt-CB: 3.6±0.2ms
Testing pattern_11... EL: 14.4±0.9ms Opt-CL: 4.1±0.0ms Opt-PM: 4.1±0.2ms Opt-CB: 4.4±0.5ms
Testing pattern_12... EL: 24.7±0.7ms Opt-CL: 5.3±0.3ms Opt-PM: 5.4±0.1ms Opt-CB: 5.4±0.2ms
Testing pattern_13... EL: 45.5±0.6ms Opt-CL: 7.6±0.5ms Opt-PM: 14.3±4.8ms Opt-CB: 9.9±3.3ms
Testing pattern_14... EL: 97.0±3.4ms Opt-CL: 13.6±0.2ms Opt-PM: 14.2±0.4ms Opt-CB: 14.8±0.9ms
Testing pattern_15... EL: 209.0±13.0ms Opt-CL: 30.3±1.1ms Opt-PM: 34.1±2.5ms Opt-CB: 34.7±1.0ms
Testing pattern_16... EL: 444.5±14.6ms Opt-CL: 74.3±2.7ms Opt-PM: 82.1±1.0ms Opt-CB: 80.7±0.8ms
Testing pattern_17... EL: 1150.0±215.0ms Opt-CL: 194.3±11.1ms Opt-PM: 254.6±4.7ms Opt-CB: 268.7±40.2ms
Testing pattern_18... EL: 2297.9±57.7ms Opt-CL: 447.0±12.4ms Opt-PM: 668.3±8.9ms Opt-CB: 706.5±33.3ms
Testing pattern_19... EL: 4987.7±87.1ms Opt-CL: 1349.2±27.5ms Opt-PM: 3217.1±68.5ms Opt-CB: 3225.0±36.5ms
Testing pattern_20... EL: 11018.7±171.7ms Opt-CL: 4768.3±70.7ms Opt-PM: 11138.8±183.8ms Opt-CB: 11166.1±214.9ms
Testing pattern_21... EL: 24440.4±503.1ms Opt-CL: 23042.7±492.3ms Opt-PM: 51287.1±1559.3ms Opt-CB: 52029.6±1060.0ms
Testing pattern_22... EL: 53905.0±795.2ms Opt-CL: 101840.8±1951.2ms ^[[AOpt-PM: 231345.0±17320.7ms Opt-CB: 241076.5±23684.6ms
Testing pattern_23... EL: 122903.9±11571.6ms Opt-CL: timeout Opt-PM: timeout Opt-CB: timeout
"""

data_benchmark = data = [
    (1,  (3.3,1.2),     (1.9,0.5),     (1.8,0.2),      (1.8,0.2),      (1.8,0.1)),
    (2,  (3.2,0.5),     (2.1,0.2),     (2.1,0.2),      (2.4,0.5),      (2.1,0.5)),
    (3,  (3.9,0.2),     (2.1,0.1),     (2.2,0.2),      (2.2,0.2),      (2.1,0.3)),
    (4,  (4.0,0.2),     (2.3,0.2),     (2.3,0.1),      (2.3,0.2),      (2.1,0.1)),
    (5,  (4.4,0.5),     (2.5,0.1),     (2.5,0.1),      (2.6,0.1),      (2.2,0.1)),
    (6,  (5.1,0.7),     (2.6,0.1),     (2.7,0.3),      (2.5,0.2),      (2.7,0.2)),
    (7,  (5.5,0.9),     (2.8,0.1),     (2.9,0.1),      (2.7,0.1),      (3.3,0.5)),
    (8,  (6.2,0.2),     (3.0,0.2),     (3.0,0.1),      (3.0,0.1),      (4.2,0.3)),
    (9,  (7.3,0.4),     (3.2,0.1),     (3.2,0.1),      (3.2,0.1),      (8.3,0.3)),
    (10, (9.8,0.3),     (3.7,0.2),     (3.6,0.1),      (3.6,0.2),      (15.8,0.7)),
    (11, (14.4,0.9),    (4.1,0.0),     (4.1,0.2),      (4.4,0.5),      (39.7,0.7)),
    (12, (24.7,0.7),    (5.3,0.3),     (5.4,0.1),      (5.4,0.2),      (94.7,3.1)),
    (13, (45.5,0.6),    (7.6,0.5),     (14.3,4.8),     (9.9,3.3),      (293.3,11.6)),
    (14, (97.0,3.4),    (13.6,0.2),    (14.2,0.4),     (14.8,0.9),     (1281.1,17.8)),
    (15, (209.0,13.0),  (30.3,1.1),    (34.1,2.5),     (34.7,1.0),     (4284.4,114.2)),
    (16, (444.5,14.6),  (74.3,2.7),    (82.1,1.0),     (80.7,0.8),     (17965.3,691.0)),
    (17, (1150.0,215.0),(194.3,11.1),  (254.6,4.7),    (268.7,40.2),   "TO"),
    (18, (2297.9,57.7), (447.0,12.4),  (668.3,8.9),    (706.5,33.3),   "TO"),
    (19, (4987.7,87.1), (1349.2,27.5), (3217.1,68.5),  (3225.0,36.5),  "TO"),
    (20, (11018.7,171.7),(4768.3,70.7),(11138.8,183.8),(11166.1,214.9), "TO"),
    (21, (24440.4,503.1),(23042.7,492.3),(51287.1,1559.3),(52029.6,1060.0), "TO"),
    (22, (53905.0,795.2),(101840.8,1951.2),(231345.0,17320.7),(241076.5,23684.6),   "TO"),
    (23, (122903.9,11571), "TO", "TO", "TO", "TO")
]

data_implication_pattern = [
    (1,  (4.1, 1.9),      (3.1, 1.2),        (2.4, 0.1),         (2.4, 0.2)),
    (2,  (4.5, 0.5),      (3.2, 0.9),        (2.8, 0.4),         (2.5, 0.1)),
    (3,  (4.2, 0.5),      (3.1, 0.5),        (3.0, 0.3),         (2.8, 0.2)),
    (4,  (5.1, 0.5),      (3.0, 0.1),        (3.0, 0.1),         (3.1, 0.2)),
    (5,  (6.6, 0.4),      (3.4, 0.1),        (3.3, 0.1),         (3.5, 0.4)),
    (6,  (13.1, 1.3),     (3.8, 0.2),        (3.7, 0.1),         (4.4, 0.6)),
    (7,  (36.8, 1.2),     (4.9, 0.3),        (4.5, 0.3),         (4.5, 0.0)),
    (8,  (156.8, 3.0),    (6.4, 0.7),        (6.5, 0.4),         (6.4, 0.7)),
    (9,  (783.6, 19.6),   (11.0, 0.3),       (12.0, 1.1),        (13.0, 1.7)),
    (10, (3918.7, 133.7), (27.8, 3.2),       (27.0, 0.9),        (29.6, 5.1)),
    (11, (17928.4, 524.5),(76.6, 4.4),       (84.5, 4.5),        (83.9, 5.6)),
    (12, (81721.6, 688.9),(266.4, 4.6),      (326.0, 18.8),      (325.5, 13.0)),
    (13, "TO",            (1075.9, 141.4),   (1645.0, 128.8),    (1602.4, 83.0)),
    (14, "ERR",           (3683.0, 227.5),   (7833.1, 121.3),    (7716.2, 416.5)),
    (15, "ERR",           (14251.1, 543.2),  (28827.2, 105.3),   (28977.5, 296.9)),
    (16, "ERR",           (65374.0, 7836.9), (126295.4, 12635.1),(125707.3, 10649.6)),
    (17, "ERR",           "ERR", "ERR" ,"ERR"),
]


def fmt(cell):
    if cell is None:
        return ""
    if cell == "TO":
        return "\\textsc{TO}"
    if cell == "ERR":
        return "\\textsc{ERR}"
    if cell == "?":
        return "?"
    val, ci = cell
    # Convert milliseconds to seconds, keep millisecond precision (3 decimal places)
    s_val = val / 1000.0
    s_ci = ci / 1000.0
    return rf"\textbf{{{s_val:.3f}s}}  {{\scriptsize $\pm${s_ci:.3f}s}}"

print(r"""\begin{table}[htbp]
\centering
\begin{tabular}{r|ccccc}
\hline
$n$ & EL & Buechi & SafeReach & CoBuechi & LTLf \\
\hline""")

for row in data:
    n, *cells = row
    line = " & ".join([str(n)] + [fmt(c) for c in cells])
    print(line + r" \\")

print(r"""\hline
\end{tabular}
\caption{Benchmark results (runtime in s with 95\% confidence interval). TO = timeout, ERR = error.}
\label{tab:benchmark_results}
\end{table}""")
print("\n"*5)


print(r"""\begin{table}[htbp]
\centering
\begin{tabular}{r|cccc}
\hline
$n$ & EL & Opt-CL & Opt-PM & Opt-CB \\
\hline""")

for n, *cells in data_implication_pattern:
    line = " & ".join([str(n)] + [fmt(c) for c in cells])
    print(line + r" \\")

print(r"""\hline
\end{tabular}
\caption{Benchmark results for the implication pattern (runtime in s with 95\% confidence interval). TO = timeout, ERR = error.}
\label{tab:implication_pattern_results}
\end{table}""")
