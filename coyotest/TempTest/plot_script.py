import pandas as pd
import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots

df = pd.read_csv('./WithoutNekara/memcached_coverage.txt')

trace2 = go.Scatter(
    x=df['x'],
    y=df['y'],
    name='Without Nekara'
)

df1 = pd.read_csv('./WithNekara/memcached_coverage.txt')

trace1 = go.Scatter(
    x=df1['x'],
    y=df1['y'],
    name='With Nekara, Random'
)

df2 = pd.read_csv('./WithNekaraNotBlock/memcached_coverage.txt')

trace3 = go.Scatter(
    x=df2['x'],
    y=df2['y'],
    name='With Nekara, Random, no block and signal'
)

fig = make_subplots(1,1);
fig.update_layout(title_text="Memcached coverage. Hash of only slab only with just 10 deletes and 10 sets");
fig.add_trace(trace1);
fig.add_trace(trace2);
fig.add_trace(trace3);

fig.show()