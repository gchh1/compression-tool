import numpy as np
import torch
import torch.nn as nn
import math

d_model = 512

# 保留更优的随机初始化设计
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')


class PositionEmbed(nn.Module):
    def __init__(self, d_model: int, max_seq_len: int, vocab_size: int=10000):
        super().__init__()
        # embedding本质上就是weight[indices],但是linear需要使用到onehot，矩阵稀疏，浪费空间
        position = torch.arange(max_seq_len).unsqueeze(1).float()
        div_term = torch.exp(torch.arange(0, d_model, 2).float() * 
                            -(math.log(vocab_size, 10) / d_model))
        # div_term2 = 1.0 / (10000 ** (torch.arange(0, d_model, 2).float() / d_model))

        self.PE = torch.zeros(max_seq_len, d_model)
        self.PE[:, 0::2] = torch.sin(position * div_term)
        self.PE[:, 1::2] = torch.cos(position * div_term)
        self.PE = self.PE.unsqueeze(0) # (1, max_seq_len, d_model)
        # self.register_buffer('PE', self.PE)

        self.embedding = nn.Embedding(vocab_size, d_model)
        
    def forward(self, X: torch.Tensor):
        """
        X: (batch_size, max_seq_len)
        return: (batch_size, max_seq_len, d_model)
        """
        self.PE = self.PE.to(X.device)
        embeded = self.embedding(X) # (batch_size, max_seq_len, d_model)
        return embeded + self.PE[:, :X.shape[1]] # 广播 (batch_size, max_seq_len, d_model)

class Mask(nn.Module):
    def __init__(self):
        super().__init__()
        # 不保存任何东西，每次 forward 时动态生成

    def __call__(self, X: torch.Tensor):
        """
        X: (batch_size, seq_len, d_model)
        return: (1, 1, seq_len, seq_len)
        
        动态生成因果掩码，根据输入序列长度自适应
        """
        seq_len = X.shape[1]
        device = X.device
        
        mask = torch.triu(
            torch.ones(seq_len, seq_len, device=device),
            diagonal=1
        ) * float('-inf')
        
        return mask.unsqueeze(0).unsqueeze(0)

   # 多头注意力机制
class MutiHeadAttention(nn.Module):
    def __init__(
        self,head_num:int,
        d_model:int,
        mode:str='self',
        ):
        super().__init__()
        self.head_num = head_num
        self.d_model = d_model
        if (d_model % head_num != 0):
            raise ValueError("d_model must be divisible by head_num")
        self.d_k = d_model // head_num

        self.mode = mode
        if self.mode == 'self':
            self.self_linear = nn.Linear(d_model, d_model * 3)
        else:
            self.self_linear = nn.Linear(d_model, d_model)
            self.other_linear = nn.Linear(d_model, d_model*2)
        # 标准 Transformer 的输出投影层 W^O
        self.out_proj = nn.Linear(d_model, d_model)

    def forward(self,SelfX:torch.Tensor,OtherX:torch.Tensor=None,mask:torch.Tensor=None):
        """
        Q: (batch_size, self_inshape, d_model)
        K: (batch_size, other_inshape, d_model)
        V: (batch_size, other_inshape, d_model)
        mask: 与 (batch, head, L_q, L_k) 可广播的加性掩码；-∞ 处 softmax 后为 0。PAD 列可用
              ``loaddata.padding_attention_bias(attention_mask)`` 生成后与因果等掩码相加。
        return: (batch_size, self_inshape, d_model)
        """
        # if (self.d_model is None):
        #     self.d_model = SelfX.shape[-1]
        #     if self.d_model % self.head_num != 0:
        #         raise ValueError("d_model must be divisible by head_num")
        #     self.d_k = self.d_model // self.head_num

        #     if OtherX is None:
        #         self.self_inshape = SelfX.shape
        #         self.self_linear = nn.Linear(self.d_model, self.d_model * 3)
        #     else:
        #         self.self_inshape = SelfX.shape
        #         self.other_inshape = OtherX.shape
        #         self.self_linear = nn.Linear(self.d_model, self.d_model)
        #         self.other_linear = nn.Linear(self.d_model, self.d_model * 2)
        self_inshape = SelfX.shape
        if self.mode != 'self':
            if OtherX is None:
                raise ValueError("OtherX must be not None if mode is not self")
            other_inshape = OtherX.shape

        if OtherX is None:
            QKV = self.self_linear(SelfX) # (batch_size, self_inshape, d_model * 3)
            Q, K, V = QKV.split(self.d_model, dim=-1) # (batch_size, self_inshape, d_model)
            Q = Q.reshape(*self_inshape[:-1], self.head_num, self.d_k)# (batch_size, self_inshape, head_num, d_k)
            K = K.reshape(*self_inshape[:-1], self.head_num, self.d_k)
            V = V.reshape(*self_inshape[:-1], self.head_num, self.d_k)
        else:
            Q = self.self_linear(SelfX)
            KV = self.other_linear(OtherX)
            K, V = KV.split(self.d_model, dim=-1) # (batch_size, other_inshape, d_model)
            Q = Q.reshape(*self_inshape[:-1], self.head_num, self.d_k)# (batch_size, self_inshape, head_num, d_k)
            K = K.reshape(*other_inshape[:-1], self.head_num, self.d_k)# (batch_size, other_inshape, head_num, d_k)
            V = V.reshape(*other_inshape[:-1], self.head_num, self.d_k)# (batch_size, other_inshape, head_num, d_k)

        # Q, K, V = Q.permute(0, 2, 1, 3), K.permute(0, 2, 1, 3), V.permute(0, 2, 1, 3)
        # 比permute更高效
        Q,K,V = Q.transpose(1,2),K.transpose(1,2),V.transpose(1,2) # (batch_size, head_num, self_inshape, d_k)
        
        tmp = Q @ K.transpose(-2,-1) / self.d_k**0.5 # (batch_size, head_num, self_inshape, other_inshape)
        if mask is not None:
            tmp = tmp + mask # (batch_size, head_num, self_inshape, self_inshape)

       
        attention = nn.Softmax(dim=-1)(tmp) @ V
        attention = attention.transpose(1,2).reshape(*self_inshape[:-1], self.d_model) #(batch_size, self_inshape, d_model)
        # 多头拼接后再经过输出线性层
        return self.out_proj(attention)

class LayerNorm(nn.Module):
    def __init__(self, d_model: int):
        super().__init__()
        self.eps = 1e-9
        self.gamma = nn.Parameter(torch.ones(d_model))
        self.beta = nn.Parameter(torch.zeros(d_model))
    def forward(self, X: torch.Tensor):
        """
        X: (batch_size, seq_len, d_model)
        return: (batch_size, seq_len, d_model)
        """
        mean = X.mean(dim=-1, keepdim=True)
        std = X.std(dim=-1, keepdim=True)
        return self.gamma * (X - mean) / (std + self.eps) + self.beta
class FeedForwardNetwork(nn.Module):
    def __init__(self, d_model: int, hidden_dim: int):
        """
        前馈网络，输入层和输出层的维度为d_model，隐藏层的维度为hidden_dim
        """
        super().__init__()
        self.d_model = d_model
        self.linear1 = nn.Linear(d_model, hidden_dim)
        self.linear2 = nn.Linear(hidden_dim, d_model)
        self.relu = nn.ReLU()
    def forward(self, X: torch.Tensor):
        """
        X: (batch_size, seq_len, d_model)
        return: (batch_size, seq_len, d_model)
        """
        shape = X.shape
        X = self.linear1(X.reshape(-1, self.d_model))
        X = self.relu(X)
        X = self.linear2(X)
        return X.reshape(*shape)


class Encoder_layer(nn.Module):
    def __init__(self, d_model: int, head_num: int, ffn_hidden_dim: int):
        super().__init__()
        self.MTA = MutiHeadAttention(head_num, d_model, mode='self')
        self.LayerNorm1 = LayerNorm(d_model)
        self.FFN = FeedForwardNetwork(d_model, ffn_hidden_dim)
        self.LayerNorm2 = LayerNorm(d_model)
    def forward(self, X: torch.Tensor, mask: torch.Tensor=None):
        """
        X: (batch_size, seq_len, d_model)
        return: (batch_size, seq_len, d_model)
        """
        attn_out = self.MTA(X, mask=mask)
        X = self.LayerNorm1(X + attn_out)
        ffn_out = self.FFN(X)
        X = self.LayerNorm2(X + ffn_out)
        return X
    
class Decoder_layer(nn.Module):
    def __init__(self, d_model: int, head_num: int, ffn_hidden_dim: int):
        super().__init__()
        self.MTA1 = MutiHeadAttention(head_num, d_model, mode='self')
        self.LayerNorm1 = LayerNorm(d_model)
        self.MTA2 = MutiHeadAttention(head_num, d_model, mode='cross')
        self.LayerNorm2 = LayerNorm(d_model)
        self.FFN = FeedForwardNetwork(d_model, ffn_hidden_dim)
        self.LayerNorm3 = LayerNorm(d_model)
    
    def forward(self, X: torch.Tensor, KV: torch.Tensor, mask: torch.Tensor=None):
        """
        X: (batch_size, seq_len, d_model)
        KV: (batch_size, seq_len, d_model)
        return: (batch_size, seq_len, d_model)
        """
        attn1 = self.MTA1(X, mask=mask)
        X = self.LayerNorm1(X + attn1)
        attn2 = self.MTA2(X, KV)
        X = self.LayerNorm2(X + attn2)
        ffn_out = self.FFN(X)
        X = self.LayerNorm3(X + ffn_out)
        return X


class Transformer(nn.Module):
    def __init__(self, max_seq_len: int, d_model: int, head_num: int, ffn_hidden_dim: int=None):
        super().__init__()
        self.position_embed = PositionEmbed(d_model, max_seq_len)
        if (ffn_hidden_dim is None):
            ffn_hidden_dim = d_model * 4
        self.encoder_layers = nn.ModuleList([Encoder_layer(d_model, head_num, ffn_hidden_dim) for _ in range(6)])
        self.decoder_layers = nn.ModuleList([Decoder_layer(d_model, head_num, ffn_hidden_dim) for _ in range(6)])
        self.linear = nn.Linear(d_model, d_model)
        self.softmax = nn.Softmax(dim=-1)

    def forward(self, X: torch.Tensor, Y: torch.Tensor, mask: torch.Tensor):
        """
        X: (batch_size, seq_len_n)
        Y: (batch_size, seq_len_m)
        return: (batch_size, seq_len_m, d_model)
        """
        X = self.position_embed(X)# (batch_size, seq_len_n, d_model)
        Y = self.position_embed(Y)# (batch_size, seq_len_m, d_model)
        # msk = self.mask(Y)# (1, 1, seq_len_m, seq_len_m)
        for encoder in self.encoder_layers:
            X = encoder(X)
        for decoder in self.decoder_layers:
            Y = decoder(Y, X, mask)
        Y = self.linear(Y)
        Y = self.softmax(Y)
        return Y
